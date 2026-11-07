#include "loader/ps4Module.h"

#include "common/guestPlatform.h"
#include "loader/elf.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <set>

namespace Loader {
namespace {
constexpr uint64_t MaxImage = 64 * 1024 * 1024;
constexpr uint64_t MaxTls   = 1024 * 1024;
bool               Range(uint64_t size, uint64_t offset, uint64_t count) {
    return offset <= size && count <= size - offset;
}
template <class T>
T Read(std::span<const uint8_t> bytes, size_t offset) {
	T value {};
	std::memcpy(&value, bytes.data() + offset, sizeof(value));
	return value;
}
bool Add(uint64_t value, int64_t delta, uint64_t& result) {
	if (delta >= 0) {
		if (uint64_t(delta) > UINT64_MAX - value) return false;
		result = value + uint64_t(delta);
	} else {
		const uint64_t magnitude = uint64_t(-(delta + 1)) + 1;
		if (magnitude > value) return false;
		result = value - magnitude;
	}
	return true;
}
std::string EncodeId(uint16_t value) {
	constexpr char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
	std::string    text;
	do {
		text.insert(text.begin(), digits[value & 63]);
		value >>= 6;
	} while (value);
	return text;
}
struct Identity {
	std::string name;
	int         major;
	int         minor;
};
} // namespace

std::optional<Ps4Module> PreparePs4Module(std::span<const uint8_t> file, uint64_t base,
                                          const SymbolDatabase& ps4_exports, std::string& error,
                                          uint64_t tls_module_id) {
	error.clear();
	const auto fail = [&error](std::string text) -> std::optional<Ps4Module> {
		error = std::move(text);
		return std::nullopt;
	};
	const auto detected = Common::ProbeGuestExecutable(file);
	if (!detected.error.empty()) return fail(detected.error);
	if (detected.platform != Common::GuestPlatform::PS4)
		return fail("PS4 loader rejects non-PS4 executables.");
	if (file.size() > MaxImage || base == 0 || (base & 0x3fff) != 0)
		return fail("PS4 file size or load base is unsupported.");
	const bool   self       = Read<uint32_t>(file, 0) == 0x1d3d154f;
	const size_t elf_offset = self ? 32 + size_t(Read<uint16_t>(file, 24)) * 32 : 0;
	const auto   header     = Read<Elf64_Ehdr>(file, elf_offset);
	if (header.e_type != ET_DYNEXEC && header.e_type != ET_DYNAMIC)
		return fail("Only relocatable PS4 executables and modules are supported.");
	std::vector<Elf64_Phdr> headers;
	for (size_t i = 0; i < header.e_phnum; ++i)
		headers.push_back(
		    Read<Elf64_Phdr>(file, elf_offset + header.e_phoff + i * sizeof(Elf64_Phdr)));
	const auto segment_bytes = [&](size_t index) -> std::optional<std::span<const uint8_t>> {
		const auto& segment = headers[index];
		if (!self) {
			if (!Range(file.size(), segment.p_offset, segment.p_filesz)) return std::nullopt;
			return file.subspan(size_t(segment.p_offset), size_t(segment.p_filesz));
		}
		for (size_t i = 0; i < Read<uint16_t>(file, 24); ++i) {
			const auto flags = Read<uint64_t>(file, 32 + i * 32);
			if ((flags & 0x800) == 0 || ((flags >> 20) & 0xfff) != index) continue;
			const auto offset = Read<uint64_t>(file, 40 + i * 32);
			const auto size   = Read<uint64_t>(file, 48 + i * 32);
			if (size != segment.p_filesz || !Range(file.size(), offset, size)) return std::nullopt;
			return file.subspan(size_t(offset), size_t(size));
		}
		return segment.p_filesz == 0 ? std::optional(std::span<const uint8_t> {}) : std::nullopt;
	};
	if (self) {
		std::set<uint16_t> payload_indices;
		for (size_t i = 0; i < Read<uint16_t>(file, 24); ++i) {
			const auto flags = Read<uint64_t>(file, 32 + i * 32);
			if ((flags & 0x800) && !payload_indices.insert(uint16_t((flags >> 20) & 0xfff)).second)
				return fail("Duplicate PS4 SELF payload index.");
		}
	}
	Ps4Module result;
	result.base                      = base;
	result.shared                    = header.e_type == ET_DYNAMIC;
	uint64_t              image_size = 0;
	std::optional<size_t> dynamic, dynlib, tls;
	for (size_t i = 0; i < headers.size(); ++i) {
		const auto& h = headers[i];
		if (h.p_type == PT_LOAD || h.p_type == PT_OS_RELRO) {
			if (h.p_filesz > h.p_memsz || !Range(MaxImage, h.p_vaddr, h.p_memsz))
				return fail("PS4 load segment exceeds supported image size.");
			if (h.p_align &&
			    ((h.p_align & (h.p_align - 1)) || h.p_vaddr % h.p_align != h.p_offset % h.p_align ||
			     base % h.p_align != 0))
				return fail("PS4 segment alignment is invalid.");
			if (h.p_memsz == 0) continue;
			for (const auto& segment: result.segments)
				if (h.p_vaddr < segment.offset + segment.size &&
				    segment.offset < h.p_vaddr + h.p_memsz)
					return fail("Overlapping PS4 load segments are unsupported.");
			result.segments.push_back({h.p_vaddr, h.p_memsz, h.p_flags});
			image_size = std::max(image_size, h.p_vaddr + h.p_memsz);
		}
		auto unique = [&](std::optional<size_t>& index) {
			if (index) return false;
			index = i;
			return true;
		};
		if ((h.p_type == PT_DYNAMIC && !unique(dynamic)) ||
		    (h.p_type == PT_OS_DYNLIBDATA && !unique(dynlib)) ||
		    (h.p_type == PT_TLS && !unique(tls)))
			return fail("Duplicate PS4 dynamic, dynlib or TLS segment.");
	}
	if (image_size == 0 || !Range(UINT64_MAX, base, image_size))
		return fail("PS4 image is empty or its load address overflows.");
	const auto mapped = [&](uint64_t offset, uint64_t size, uint32_t flags = 0) {
		return std::ranges::any_of(result.segments, [&](const Ps4Segment& h) {
			return offset >= h.offset && Range(h.size, offset - h.offset, size) &&
			       (h.flags & flags) == flags;
		});
	};
	result.image.resize(size_t(image_size));
	for (size_t i = 0; i < headers.size(); ++i) {
		const auto& h = headers[i];
		if ((h.p_type != PT_LOAD && h.p_type != PT_OS_RELRO) || h.p_memsz == 0) continue;
		const auto bytes = segment_bytes(i);
		if (!bytes) return fail("PS4 load segment has no valid file payload.");
		std::copy(bytes->begin(), bytes->end(), result.image.begin() + h.p_vaddr);
	}
	if (!result.shared || header.e_entry != 0) {
		if (!mapped(header.e_entry, 1, PF_X))
			return fail("PS4 entry point is not in executable memory.");
		result.entry = base + header.e_entry;
	}
	if (!dynamic || !dynlib) return fail("PS4 module requires PT_DYNAMIC and PT_SCE_DYNLIBDATA.");
	const auto dynamic_bytes = segment_bytes(*dynamic), dynlib_bytes = segment_bytes(*dynlib);
	if (!dynamic_bytes || !dynlib_bytes || dynamic_bytes->size() % 16 ||
	    dynamic_bytes->size() > 65536)
		return fail("Invalid PS4 dynamic table payload.");
	std::map<int64_t, uint64_t> tags;
	std::vector<uint64_t>       libraries, modules;
	bool                        terminated = false;
	for (size_t i = 0; i < dynamic_bytes->size(); i += 16) {
		const auto tag   = Read<int64_t>(*dynamic_bytes, i);
		const auto value = Read<uint64_t>(*dynamic_bytes, i + 8);
		if (tag == 0) {
			terminated = true;
			break;
		}
		if (tag == DT_OS_IMPORT_LIB || tag == DT_OS_IMPORT_LIB_1) {
			libraries.push_back(value);
			continue;
		}
		if (tag == DT_OS_NEEDED_MODULE || tag == DT_OS_NEEDED_MODULE_1) {
			modules.push_back(value);
			continue;
		}
		if (!tags.emplace(tag, value).second) return fail("Duplicate PS4 dynamic tag.");
		if (tag == DT_REL || tag == DT_TEXTREL || tag == 1 || tag == 35 || tag == 36 || tag == 37)
			return fail("PS4 module dependencies, REL/RELR or text relocations are unsupported in "
			            "this increment.");
	}
	if (!terminated) return fail("Unterminated PS4 dynamic table.");
	const auto table = [&](int64_t offset,
	                       int64_t length) -> std::optional<std::span<const uint8_t>> {
		if (!tags.contains(offset) || !tags.contains(length) ||
		    !Range(dynlib_bytes->size(), tags[offset], tags[length]))
			return std::nullopt;
		return dynlib_bytes->subspan(size_t(tags[offset]), size_t(tags[length]));
	};
	const auto strings = table(DT_OS_STRTAB, DT_OS_STRSZ),
	           symbols = table(DT_OS_SYMTAB, DT_OS_SYMTABSZ);
	if (!strings || !symbols || symbols->empty() || symbols->size() % sizeof(Elf64_Sym) ||
	    tags[DT_OS_SYMENT] != sizeof(Elf64_Sym))
		return fail("Invalid PS4 string or symbol table.");
	const auto text = [&](uint64_t offset) -> std::optional<std::string> {
		if (offset >= strings->size()) return std::nullopt;
		const auto begin = strings->begin() + offset,
		           end   = std::find(begin, strings->end(), uint8_t {0});
		if (end == strings->end()) return std::nullopt;
		return std::string(begin, end);
	};
	std::map<std::string, Identity> library_ids, module_ids;
	for (auto value: libraries) {
		const auto name = text(uint32_t(value));
		if (!name || name->empty() ||
		    !library_ids
		         .emplace(EncodeId(uint16_t(value >> 48)),
		                  Identity {*name, int((value >> 32) & 0xffff), 0})
		         .second)
			return fail("Invalid PS4 imported library identity.");
	}
	for (auto value: modules) {
		const auto name = text(uint32_t(value));
		if (!name || name->empty() ||
		    !module_ids
		         .emplace(EncodeId(uint16_t(value >> 48)),
		                  Identity {*name, int((value >> 40) & 0xff), int((value >> 32) & 0xff)})
		         .second)
			return fail("Invalid PS4 imported module identity.");
	}
	if (tls) {
		const auto& h = headers[*tls];
		if (tls_module_id == 0 || h.p_filesz > h.p_memsz || h.p_memsz == 0 || h.p_memsz > MaxTls ||
		    !mapped(h.p_vaddr, h.p_memsz) || h.p_align > 65536 ||
		    (h.p_align && (h.p_align & (h.p_align - 1))))
			return fail("Invalid PS4 TLS template.");
		result.tls_alignment = std::max(uint64_t {1}, h.p_align);
		result.tls_memory_size = h.p_memsz;
		result.tls_static_size =
		    (h.p_memsz + std::max(uint64_t{32}, result.tls_alignment) - 1) & ~(std::max(uint64_t{32}, result.tls_alignment) - 1);
		const auto bytes = segment_bytes(*tls);
		if (!bytes || !std::equal(bytes->begin(), bytes->end(), result.image.begin() + h.p_vaddr))
			return fail("PS4 TLS initial image differs from its mapped segment.");
	}
	std::set<uint64_t> targets;
	const auto         relocate = [&](std::span<const uint8_t> records) -> std::string {
        if (records.size() % sizeof(Elf64_Rela)) return "Invalid PS4 relocation table size.";
        for (size_t i = 0; i < records.size(); i += sizeof(Elf64_Rela)) {
            const auto r    = Read<Elf64_Rela>(records, i);
            const auto type = r.GetType(), index = r.GetSymbol();
            if (type == 0) continue;
            if (type != 1 && type != 6 && type != 7 && type != 8 && type != 16 && type != 17 &&
                type != 18)
                return "Unsupported PS4 relocation type: " + std::to_string(type);
            if (!mapped(r.r_offset, 8, PF_W) || !targets.insert(r.r_offset).second)
                return "Invalid or duplicate PS4 relocation target.";
            if (index >= symbols->size() / sizeof(Elf64_Sym))
                return "PS4 relocation symbol index exceeds its table.";
            const auto symbol = Read<Elf64_Sym>(*symbols, size_t(index) * sizeof(Elf64_Sym));
            uint64_t   value = 0;
            if (type == 8) {
                if (index || !Add(base, r.r_addend, value))
                    return "Invalid PS4 relative relocation.";
            } else if (type == 16 || type == 17 || type == 18) {
                if (!tls || ((type != 16 || index != 0) &&
                             ((symbol.st_info & 15) != 6 || symbol.st_shndx == 0 ||
                              !Range(headers[*tls].p_memsz, symbol.st_value, symbol.st_size))))
                    return "Invalid PS4 TLS relocation symbol.";
                if (type == 16) {
                    if (r.r_addend) return "External PS4 TLS module relocation is unsupported.";
                    value = tls_module_id;
                } else {
                    if (!Add(symbol.st_value, r.r_addend, value) || value >= headers[*tls].p_memsz)
                        return "PS4 TLS offset exceeds template.";
                    if (type == 18) value -= result.tls_static_size;
                }
            } else {
                if (index == 0) return "PS4 absolute relocation requires a symbol.";
                const auto kind = symbol.st_info & 15;
                if ((symbol.st_info >> 4) > 2 ||
                    (symbol.st_shndx >= 0xff00 && symbol.st_shndx != 0xfff1))
                    return "Unsupported PS4 symbol binding or special section.";
                if (symbol.st_shndx == 0 && (symbol.st_info >> 4) == 0)
                    return "Undefined local PS4 symbol is invalid.";
                if (kind != 1 && kind != 2) return "Unsupported PS4 symbol type.";
                if (symbol.st_shndx == 0) {
                    const auto name = text(symbol.st_name);
                    if (!name) return "Unterminated PS4 import name.";
                    const auto first = name->find('#'), last = name->rfind('#');
                    if (first != 11 || first == last || name->find('#', first + 1) != last)
                        return "PS4 imports require NID#library#module identities.";
                    const auto library =
                                   library_ids.find(name->substr(first + 1, last - first - 1)),
                               module = module_ids.find(name->substr(last + 1));
                    if (library == library_ids.end() || module == module_ids.end())
                        return "PS4 import identity is not declared.";
                    const SymbolResolve query {name->substr(0, first),
                                               library->second.name,
                                               library->second.major,
                                               module->second.name,
                                               module->second.major,
                                               module->second.minor,
                                               kind == 2 ? SymbolType::Func : SymbolType::Object};
                    const auto*         resolved = ps4_exports.Find(query);
                    if (resolved) {
                        if (resolved->vaddr == 0)
                            return "PS4 import registry contains a null address.";
                        value = resolved->vaddr;
                        result.resolved_imports.push_back(*name);
                    } else if ((symbol.st_info >> 4) != 2)
                        return "Unresolved PS4 import: " + *name;
                } else if (symbol.st_shndx == 0xfff1)
                    value = symbol.st_value;
                else {
                    if (!mapped(symbol.st_value, std::max(uint64_t {1}, symbol.st_size),
                                kind == 2 ? PF_X : 0))
                        return "PS4 defined symbol is outside its segment.";
                    value = base + symbol.st_value;
                }
                if (!Add(value, type == 6 || type == 7 ? 0 : r.r_addend, value))
                    return "PS4 symbol relocation overflows.";
            }
            std::memcpy(result.image.data() + r.r_offset, &value, sizeof(value));
        }
        return {};
	};
	if (tags.contains(DT_OS_RELA) || tags.contains(DT_OS_RELASZ)) {
		const auto records = table(DT_OS_RELA, DT_OS_RELASZ);
		if (!records || tags[DT_OS_RELAENT] != sizeof(Elf64_Rela))
			return fail("Invalid PS4 RELA table.");
		if (auto reason = relocate(*records); !reason.empty()) return fail(reason);
	}
	if (tags.contains(DT_OS_JMPREL) || tags.contains(DT_OS_PLTRELSZ)) {
		const auto records = table(DT_OS_JMPREL, DT_OS_PLTRELSZ);
		if (!records || tags[DT_OS_PLTREL] != DT_RELA) return fail("Invalid PS4 PLT RELA table.");
		if (auto reason = relocate(*records); !reason.empty()) return fail(reason);
	}
	if (tags.contains(DT_RELA) || tags.contains(DT_JMPREL) || tags.contains(DT_SYMTAB))
		return fail("Generic dynamic symbol/relocation tables are unsupported for PS4.");
	if (tls) {
		const auto& h = headers[*tls];
		result.tls_template.assign(size_t(result.tls_static_size), 0);
		std::copy_n(result.image.begin() + h.p_vaddr, size_t(h.p_memsz),
		            result.tls_template.begin());
	}
	if (tags.contains(DT_INIT)) {
		if (!mapped(tags[DT_INIT], 1, PF_X)) return fail("PS4 initializer is not executable.");
		result.initializers.push_back(base + tags[DT_INIT]);
	}
	for (const auto [address_tag, size_tag]: {std::pair {DT_PREINIT_ARRAY, DT_PREINIT_ARRAYSZ},
	                                          std::pair {DT_INIT_ARRAY, DT_INIT_ARRAYSZ}}) {
		if (!tags.contains(address_tag) && !tags.contains(size_tag)) continue;
		if (!tags.contains(address_tag) || !tags.contains(size_tag) || tags[size_tag] % 8 ||
		    !mapped(tags[address_tag], tags[size_tag]))
			return fail("Invalid PS4 initializer array.");
		std::vector<uint64_t> entries;
		for (uint64_t offset = 0; offset < tags[size_tag]; offset += 8) {
			const auto address = Read<uint64_t>(result.image, size_t(tags[address_tag] + offset));
			if (address < base || !mapped(address - base, 1, PF_X))
				return fail("PS4 initializer array points outside executable memory.");
			entries.push_back(address);
		}
		if (address_tag == DT_PREINIT_ARRAY)
			result.initializers.insert(result.initializers.begin(), entries.begin(), entries.end());
		else
			result.initializers.insert(result.initializers.end(), entries.begin(), entries.end());
	}
	if (tags.contains(DT_FINI_ARRAY) || tags.contains(DT_FINI_ARRAYSZ)) {
		if (!tags.contains(DT_FINI_ARRAY) || !tags.contains(DT_FINI_ARRAYSZ) ||
		    tags[DT_FINI_ARRAYSZ] % 8 || !mapped(tags[DT_FINI_ARRAY], tags[DT_FINI_ARRAYSZ]))
			return fail("Invalid PS4 finalizer array.");
		for (uint64_t offset = tags[DT_FINI_ARRAYSZ]; offset != 0; offset -= 8) {
			const auto address =
			    Read<uint64_t>(result.image, size_t(tags[DT_FINI_ARRAY] + offset - 8));
			if (address < base || !mapped(address - base, 1, PF_X))
				return fail("PS4 finalizer array points outside executable memory.");
			result.finalizers.push_back(address);
		}
	}
	if (tags.contains(DT_FINI)) {
		if (!mapped(tags[DT_FINI], 1, PF_X)) return fail("PS4 finalizer is not executable.");
		result.finalizers.push_back(base + tags[DT_FINI]);
	}
	return result;
}
} // namespace Loader
