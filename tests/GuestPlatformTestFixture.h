#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace GuestPlatformTests {
inline void Put(std::vector<uint8_t>& bytes, size_t offset, uint64_t value, size_t size) {
	for (size_t i = 0; i < size; ++i)
		bytes.at(offset + i) = static_cast<uint8_t>(value >> (8 * i));
}
inline std::vector<uint8_t> Elf(bool ps5) {
	std::vector<uint8_t> bytes(128);
	Put(bytes, 0, 0x464c457f, 4);
	bytes[4] = 2;
	bytes[5] = 1;
	bytes[6] = 1;
	bytes[7] = 9;
	bytes[8] = ps5 ? 2 : 0;
	Put(bytes, 16, 0xfe10, 2);
	Put(bytes, 18, 62, 2);
	Put(bytes, 20, 1, 4);
	Put(bytes, 24, 0x1000, 8);
	Put(bytes, 32, 64, 8);
	Put(bytes, 52, 64, 2);
	Put(bytes, 54, 56, 2);
	Put(bytes, 56, 1, 2);
	Put(bytes, 64, 1, 4);
	Put(bytes, 68, 5, 4);
	Put(bytes, 72, 120, 8);
	Put(bytes, 80, 0x1000, 8);
	Put(bytes, 96, 8, 8);
	Put(bytes, 104, 8, 8);
	return bytes;
}
inline std::vector<uint8_t> Self(bool ps5) {
	const auto           elf = Elf(ps5);
	std::vector<uint8_t> bytes(64 + elf.size());
	Put(bytes, 0, ps5 ? 0xeef51454 : 0x1d3d154f, 4);
	bytes[5] = 1;
	bytes[6] = 1;
	bytes[7] = 0x12;
	Put(bytes, 24, 1, 2);
	Put(bytes, 32, 0x800, 8);
	Put(bytes, 40, 184, 8);
	Put(bytes, 48, 8, 8);
	Put(bytes, 56, 8, 8);
	std::copy(elf.begin(), elf.end(), bytes.begin() + 64);
	return bytes;
}
inline std::vector<uint8_t> Sfo() {
	const std::vector<std::pair<std::string, std::string>> entries {
	    {"TITLE", "PS4 synthetic test"}, {"TITLE_ID", "TEST00001"}, {"APP_VER", "01.00"}};
	std::vector<uint8_t> bytes(68);
	Put(bytes, 0, 0x46535000, 4);
	Put(bytes, 4, 0x101, 4);
	Put(bytes, 8, bytes.size(), 4);
	Put(bytes, 16, entries.size(), 4);
	for (size_t i = 0; i < entries.size(); ++i) {
		Put(bytes, 20 + i * 16, bytes.size() - 68, 2);
		bytes.insert(bytes.end(), entries[i].first.begin(), entries[i].first.end());
		bytes.push_back(0);
	}
	const auto values = bytes.size();
	Put(bytes, 12, values, 4);
	for (size_t i = 0; i < entries.size(); ++i) {
		Put(bytes, 22 + i * 16, 0x204, 2);
		Put(bytes, 24 + i * 16, entries[i].second.size() + 1, 4);
		Put(bytes, 28 + i * 16, entries[i].second.size() + 1, 4);
		Put(bytes, 32 + i * 16, bytes.size() - values, 4);
		bytes.insert(bytes.end(), entries[i].second.begin(), entries[i].second.end());
		bytes.push_back(0);
	}
	return bytes;
}
} // namespace GuestPlatformTests
