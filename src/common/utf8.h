#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Common {
inline bool IsValidUtf8(std::span<const uint8_t> bytes) {
	for (size_t i = 0; i < bytes.size();) {
		const auto first = bytes[i++];
		if (first < 0x80) continue;
		const size_t extra = first >= 0xf0 && first <= 0xf4   ? 3
		                     : first >= 0xe0 && first <= 0xef ? 2
		                     : first >= 0xc2 && first <= 0xdf ? 1
		                                                      : 0;
		if (extra == 0 || extra > bytes.size() - i) return false;
		uint32_t code = first & (0x7f >> extra);
		for (size_t j = 0; j < extra; ++j) {
			const auto next = bytes[i++];
			if ((next & 0xc0) != 0x80) return false;
			code = (code << 6) | (next & 0x3f);
		}
		if (code < (extra == 1   ? 0x80u
		            : extra == 2 ? 0x800u
		                         : 0x10000u) ||
		    code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
			return false;
	}
	return true;
}
} // namespace Common
