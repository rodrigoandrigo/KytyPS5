#include "graphics/guest_gpu/ps4Transfer.h"

#include <limits>

namespace Libs::Graphics::Ps4 {
namespace {
constexpr uint64_t MaxBytes = 16 * 1024 * 1024;
bool               Fail(std::string& error, const std::string& message) {
    error = message;
    return false;
}
bool ValidateTarget(LinearTarget t, std::string& error) {
	const uint64_t size = uint64_t(t.pitch) * t.height;
	if (!t.width || !t.height || uint64_t(t.width) * 4 > t.pitch || t.pitch % 256 || !size ||
	    size > MaxBytes || t.address % 4 || t.address > UINT64_MAX - size)
		return Fail(error, "Invalid linear RGBA8 target (size, address or 256-byte pitch)");
	return true;
}
bool Add(TransferPlan& plan, uint64_t address, std::span<const uint32_t> data, uint64_t& total,
         std::string& error) {
	const uint64_t size = uint64_t(plan.target.pitch) * plan.target.height;
	if (address % 4 || address < plan.target.address || address - plan.target.address > size ||
	    data.size_bytes() > size - (address - plan.target.address))
		return Fail(error, "PM4 write outside supplied target");
	if (plan.writes.size() >= 4096 || data.size_bytes() > MaxBytes - total)
		return Fail(error, "PM4 transfer budget exceeded");
	total += data.size_bytes();
	plan.writes.push_back({uint32_t(address - plan.target.address), {data.begin(), data.end()}});
	return true;
}
} // namespace

bool ValidatePlan(const TransferPlan& plan, std::string& error) {
	error.clear();
	if (!ValidateTarget(plan.target, error)) return false;
	uint64_t       total = 0;
	const uint64_t size  = uint64_t(plan.target.pitch) * plan.target.height;
	if (plan.writes.size() > 4096) return Fail(error, "Too many transfers");
	for (const auto& write: plan.writes) {
		if (write.offset % 4 || write.offset > size ||
		    write.words.size() > (size - write.offset) / 4 ||
		    write.words.size() > (MaxBytes - total) / 4)
			return Fail(error, "Invalid transfer plan range or budget");
		total += write.words.size() * 4;
	}
	return true;
}

bool DecodeTransfers(std::span<const uint32_t> commands, LinearTarget target, TransferPlan& output,
                     std::string& error) {
	error.clear();
	if (!ValidateTarget(target, error)) return false;
	if (commands.size() > MaxBytes / 4) return Fail(error, "PM4 stream budget exceeded");
	TransferPlan plan {target, {}};
	uint64_t     total = 0;
	for (size_t pos = 0; pos < commands.size();) {
		const uint32_t header = commands[pos];
		if (header >> 30 != 3 || (header & 0xff))
			return Fail(error, "Only unpredicated graphics type-3 PM4 packets are supported");
		const size_t count = ((header >> 16) & 0x3fff) + 2;
		if (count > commands.size() - pos) return Fail(error, "Truncated PS4 PM4 packet");
		const auto     packet = commands.subspan(pos, count);
		const uint32_t opcode = (header >> 8) & 0xff;
		if (opcode == 0x10) {
			// PS4 NOP payloads are opaque. Never interpret Kyty's PS5 NOP subcodes.
		} else if (opcode == 0x37) {
			// Destination 5 = memory, incrementing address; confirmation is optional.
			if (count < 5 || (packet[1] & ~(0x500u | (1u << 20))) || (packet[1] & 0xf00) != 0x500)
				return Fail(error, "Unsupported PS4 WRITE_DATA controls");
			const uint64_t address = packet[2] | (uint64_t(packet[3]) << 32);
			if (!Add(plan, address, packet.subspan(4), total, error)) return false;
		} else if (opcode == 0x50) {
			// Immediate source (2), memory destination (0), optional CP_SYNC.
			if (count != 7 || (packet[1] & ~(0x40000000u | 0x80000000u)) ||
			    (packet[1] & 0x60000000u) != 0x40000000u || packet[3] || (packet[6] & ~0x1fffffu) ||
			    !(packet[6] & 0x1fffff) || packet[6] % 4)
				return Fail(error, "Unsupported PS4 DMA_DATA controls");
			const uint32_t bytes = packet[6];
			if (bytes > MaxBytes - total) return Fail(error, "PM4 transfer budget exceeded");
			const uint64_t              address = packet[4] | (uint64_t(packet[5]) << 32);
			const std::vector<uint32_t> fill(bytes / 4, packet[2]);
			if (!Add(plan, address, fill, total, error)) return false;
		} else {
			return Fail(error, "Unsupported PS4 PM4 opcode " + std::to_string(opcode) +
			                       " at dword " + std::to_string(pos));
		}
		pos += count;
	}
	output = std::move(plan);
	return true;
}

std::vector<uint32_t> MakeTransferFixture(LinearTarget t) {
	// Width/height must have been validated by the caller before executing.
	std::vector<uint32_t> words {0xc0055000,          0xc0000000,
	                             0xff204080,          0,
	                             uint32_t(t.address), uint32_t(t.address >> 32),
	                             t.pitch * t.height};
	for (uint32_t y = 0; y < t.height; ++y) {
		const uint32_t start = y < t.height / 2 ? t.width / 2 : 0;
		const uint32_t count = y < t.height / 2 ? t.width - start : t.width / 2;
		if (!count) continue;
		const uint64_t address = t.address + uint64_t(y) * t.pitch + start * 4;
		words.insert(words.end(), {0xc0003700u | ((count + 2) << 16), 0x00100500, uint32_t(address),
		                           uint32_t(address >> 32)});
		for (uint32_t x = 0; x < count; ++x)
			words.push_back(0xffc08020);
	}
	return words;
}
std::vector<uint8_t> ExpectedTransferFixture(LinearTarget t) {
	std::vector<uint8_t> pixels;
	for (uint32_t y = 0; y < t.height; ++y)
		for (uint32_t x = 0; x < t.width; ++x) {
			const bool a = (x < t.width / 2) == (y < t.height / 2);
			pixels.insert(pixels.end(), {uint8_t(a ? 128 : 32), uint8_t(a ? 64 : 128),
			                             uint8_t(a ? 32 : 192), 255});
		}
	return pixels;
}
} // namespace Libs::Graphics::Ps4
