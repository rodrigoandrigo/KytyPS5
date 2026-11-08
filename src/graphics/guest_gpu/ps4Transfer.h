#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Libs::Graphics::Ps4 {
// An explicitly supplied linear RGBA8 target, not a decoded GCN image descriptor.
// Pitch includes row padding and is in bytes. No guest pointers are dereferenced.
struct LinearTarget {
	uint64_t address;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
};
struct Transfer {
	uint32_t              offset;
	std::vector<uint32_t> words;
};
struct TransferPlan {
	LinearTarget          target {};
	std::vector<Transfer> writes;
};
// Transactional, bounded PS4 PM4 subset: NOP, WRITE_DATA (incrementing memory),
// DMA_DATA (immediate dword fill to memory). Unsupported controls/packets fail.
bool DecodeTransfers(std::span<const uint32_t> commands, LinearTarget target, TransferPlan& output,
                     std::string& error);
bool ValidatePlan(const TransferPlan& plan, std::string& error);
// Own fixture; genuine type-3 packets, a fill followed by ordered row overwrites.
std::vector<uint32_t> MakeTransferFixture(LinearTarget target);
std::vector<uint8_t>  ExpectedTransferFixture(LinearTarget target);
} // namespace Libs::Graphics::Ps4
