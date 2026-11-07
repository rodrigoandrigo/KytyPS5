#include "common/guestPlatform.h"

#include "common/utf8.h"

#include <algorithm>
#include <array>
#include <limits>

namespace Common {
namespace {
uint16_t U16(std::span<const uint8_t> b, size_t p) {
	return uint16_t(b[p]) | (uint16_t(b[p + 1]) << 8);
}
uint32_t U32(std::span<const uint8_t> b, size_t p) {
	return U16(b, p) | (uint32_t(U16(b, p + 2)) << 16);
}
uint64_t U64(std::span<const uint8_t> b, size_t p) {
	return U32(b, p) | (uint64_t(U32(b, p + 4)) << 32);
}
bool Range(uint64_t size, uint64_t offset, uint64_t count) {
	return offset <= size && count <= size - offset;
}
GuestDetection Fail(const char* error) {
	return {GuestPlatform::Unknown, error};
}
} // namespace

const char* GuestPlatformName(GuestPlatform platform) {
	switch (platform) {
		case GuestPlatform::PS4: return "PS4";
		case GuestPlatform::PS5: return "PS5";
		default: return "Unknown";
	}
}

std::optional<GuestPlatform> ParseGuestPlatform(std::string_view value) {
	if (value == "auto" || value == "Unknown") return GuestPlatform::Unknown;
	if (value == "ps4" || value == "PS4") return GuestPlatform::PS4;
	if (value == "ps5" || value == "PS5") return GuestPlatform::PS5;
	return std::nullopt;
}

GuestDetection ProbeGuestExecutable(uint64_t size, const GuestRead& read) {
	std::array<uint8_t, 64> header {};
	if (size < header.size() || !read(0, header))
		return Fail("Executable header is missing or truncated.");
	uint64_t       elf           = 0;
	GuestPlatform  container     = GuestPlatform::Unknown;
	uint16_t       payload_count = 0;
	const uint32_t magic         = U32(header, 0);
	if (magic == 0x1d3d154f || magic == 0xeef51454) {
		container = magic == 0x1d3d154f ? GuestPlatform::PS4 : GuestPlatform::PS5;
		if (header[5] != 1 || header[6] != 1 || header[7] != 0x12)
			return Fail("Unsupported SELF header layout.");
		payload_count = U16(header, 24);
		if (payload_count > 4096) return Fail("SELF payload table exceeds the supported limit.");
		elf = 32 + uint64_t(payload_count) * 32;
		if (!Range(size, elf, header.size()) || !read(elf, header))
			return Fail("SELF ELF header is truncated.");
	} else if (magic != 0x464c457f) {
		return Fail("Unknown executable format; expected a decrypted ELF or SELF.");
	}
	if (U32(header, 0) != 0x464c457f || header[4] != 2 || header[5] != 1 || header[6] != 1 ||
	    header[7] != 9 || U16(header, 18) != 62 || U32(header, 20) != 1 || U16(header, 52) != 64)
		return Fail("Unsupported ELF header; expected FreeBSD x86-64 little-endian ELF64.");
	const auto type = U16(header, 16);
	if (type != 0xfe00 && type != 0xfe10 && type != 0xfe18)
		return Fail("Unsupported ELF type; expected a console executable or module.");
	GuestPlatform platform = header[8] == 0   ? GuestPlatform::PS4
	                         : header[8] == 2 ? GuestPlatform::PS5
	                                          : GuestPlatform::Unknown;
	if (platform == GuestPlatform::Unknown) return Fail("Unknown console ELF ABI version.");
	if (platform == GuestPlatform::PS5 && type == 0xfe00)
		return Fail("Unsupported PS5 ELF executable type.");
	if (container != GuestPlatform::Unknown && container != platform)
		return Fail("SELF signature and ELF ABI identify different platforms.");
	const uint64_t phoff = U64(header, 32);
	const uint64_t count = U16(header, 56);
	if (U16(header, 54) != 56 || count == 0 || count > 4096 || phoff < 64 || phoff > size - elf ||
	    !Range(size - elf, phoff, count * 56))
		return Fail("ELF program header table is invalid or truncated.");
	for (uint64_t i = 0; i < payload_count; ++i) {
		std::array<uint8_t, 32> payload {};
		if (!read(32 + i * 32, payload)) return Fail("Cannot read SELF payload table.");
		const auto flags = U64(payload, 0);
		if ((flags & 0x800) == 0) continue;
		const auto index = (flags >> 20) & 0xfff;
		if (index >= count || !Range(size, U64(payload, 8), U64(payload, 16)))
			return Fail("SELF payload index or range is invalid.");
		if ((flags & 0xa) != 0 || U64(payload, 16) != U64(payload, 24))
			return Fail(
			    "Encrypted or compressed SELF payloads must be decrypted and unpacked first.");
	}
	for (uint64_t i = 0; i < count; ++i) {
		std::array<uint8_t, 56> segment {};
		if (!read(elf + phoff + i * 56, segment)) return Fail("Cannot read ELF program headers.");
		const auto file_size   = U64(segment, 32);
		const auto memory_size = U64(segment, 40);
		if ((U32(segment, 0) == 1 || U32(segment, 0) == 0x61000010) && file_size > memory_size)
			return Fail("ELF segment file size exceeds memory size.");
		if (U64(segment, 16) > std::numeric_limits<uint64_t>::max() - memory_size)
			return Fail("ELF segment address overflows.");
		if (container == GuestPlatform::Unknown && !Range(size, U64(segment, 8), file_size))
			return Fail("ELF segment extends beyond the file.");
	}
	return {platform, {}};
}

GuestDetection ProbeGuestExecutable(std::span<const uint8_t> image) {
	return ProbeGuestExecutable(image.size(), [image](uint64_t offset, std::span<uint8_t> target) {
		if (!Range(image.size(), offset, target.size())) return false;
		std::copy_n(image.data() + offset, target.size(), target.data());
		return true;
	});
}

SfoMetadata ReadSfoMetadata(std::span<const uint8_t> bytes) {
	const auto fail = [](const char* error) { return SfoMetadata {{}, error}; };
	if (bytes.size() < 20 || bytes.size() > 1024 * 1024 || U32(bytes, 0) != 0x46535000 ||
	    (U32(bytes, 4) != 0x101 && U32(bytes, 4) != 0x100))
		return fail("Invalid param.sfo header.");
	const uint64_t keys = U32(bytes, 8), values = U32(bytes, 12), count = U32(bytes, 16);
	if (count > 4096 || !Range(bytes.size(), 20, count * 16) || keys < 20 + count * 16 ||
	    values < keys || values > bytes.size())
		return fail("Invalid param.sfo table bounds.");
	SfoMetadata result;
	for (size_t i = 0; i < count; ++i) {
		const size_t   entry = 20 + i * 16;
		const uint64_t key = keys + U16(bytes, entry), offset = values + U32(bytes, entry + 12);
		const uint64_t length = U32(bytes, entry + 4), capacity = U32(bytes, entry + 8);
		if (key >= values || length > capacity || !Range(bytes.size(), offset, capacity))
			return fail("Invalid param.sfo entry bounds.");
		const auto end = std::find(bytes.begin() + key, bytes.begin() + values, uint8_t {0});
		if (end == bytes.begin() + values) return fail("Unterminated param.sfo key.");
		const std::string name(bytes.begin() + key, end);
		const auto        format = U16(bytes, entry + 2);
		if (format == 0x204) {
			if (length == 0 || bytes[offset + length - 1] != 0)
				return fail("Unterminated param.sfo string.");
			const auto value =
			    bytes.subspan(static_cast<size_t>(offset), static_cast<size_t>(length - 1));
			if (!IsValidUtf8(value) ||
			    std::find(value.begin(), value.end(), uint8_t {0}) != value.end())
				return fail("Invalid UTF-8 param.sfo string.");
			if (!result.strings
			         .emplace(name, std::string(bytes.begin() + offset,
			                                    bytes.begin() + offset + length - 1))
			         .second)
				return fail("Duplicate param.sfo string key.");
		} else if (format != 0x4 && (format != 0x404 || length != 4)) {
			return fail("Unsupported param.sfo entry format.");
		}
	}
	return result;
}

GuestDetection ResolveGuestProfile(const GuestDetection& detected, GuestPlatform requested) {
	if (!detected.error.empty()) return detected;
	if (detected.platform == GuestPlatform::Unknown)
		return Fail("The executable platform could not be identified.");
	if (requested != GuestPlatform::Unknown && requested != detected.platform)
		return Fail("The requested platform conflicts with the executable platform; override "
		            "cannot bypass validation.");
	return {requested == GuestPlatform::Unknown ? detected.platform : requested, {}};
}

std::string GuestLaunchError(const GuestDetection& profile) {
	if (!profile.error.empty()) return profile.error;
	if (profile.platform == GuestPlatform::PS4)
		return "PS4 title recognized. PS4 game execution needs an implemented graphics profile; "
		       "the PS5 runtime will not "
		       "be started.";
	if (profile.platform != GuestPlatform::PS5) return "The game platform is unknown.";
	return {};
}
} // namespace Common
