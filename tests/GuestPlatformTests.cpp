#include "GuestPlatformTestFixture.h"
#include "common/guestPlatform.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
void Check(bool value, const char* message) {
	if (!value) {
		std::fprintf(stderr, "GuestPlatformTests: %s\n", message);
		std::abort();
	}
}
} // namespace
int main() {
	using namespace Common;
	using namespace GuestPlatformTests;
	for (bool ps5: {false, true}) {
		const auto expected = ps5 ? GuestPlatform::PS5 : GuestPlatform::PS4;
		for (bool self: {false, true}) {
			const auto image  = self ? Self(ps5) : Elf(ps5);
			const auto result = ProbeGuestExecutable(image);
			Check(result.error.empty() && result.platform == expected, "ELF/SELF platform");
			for (size_t size = 0; size < image.size(); ++size)
				Check(!ProbeGuestExecutable(std::span(image).first(size)).error.empty(),
				      "reject every truncated header/table/ELF payload");
			Check(ResolveGuestProfile(result, GuestPlatform::Unknown).platform == expected,
			      "auto preserves detection");
			Check(ResolveGuestProfile(result, expected).platform == expected,
			      "matching manual selection");
			Check(!ResolveGuestProfile(result, ps5 ? GuestPlatform::PS4 : GuestPlatform::PS5)
			           .error.empty(),
			      "override cannot change executable ABI");
			Check(GuestLaunchError(result).empty() == ps5, "only PS5 can enter runtime");
		}
	}
	auto bad = Elf(false);
	Put(bad, 16, 3, 2);
	Check(!ProbeGuestExecutable(bad).error.empty(), "generic ABI 0 ELF is not PS4");
	bad    = Elf(true);
	bad[8] = 1;
	Check(!ProbeGuestExecutable(bad).error.empty(), "unknown ABI");
	bad    = Elf(true);
	bad[7] = 0;
	Check(!ProbeGuestExecutable(bad).error.empty(), "wrong OSABI");
	bad = Elf(true);
	Put(bad, 18, 183, 2);
	Check(!ProbeGuestExecutable(bad).error.empty(), "wrong CPU architecture");
	bad = Elf(true);
	Put(bad, 32, std::numeric_limits<uint64_t>::max(), 8);
	Check(!ProbeGuestExecutable(bad).error.empty(), "overflowing program table offset");
	bad = Elf(true);
	Put(bad, 56, 65535, 2);
	Check(!ProbeGuestExecutable(bad).error.empty(), "excessive program table count");
	bad = Elf(true);
	Put(bad, 96, 9, 8);
	Check(!ProbeGuestExecutable(bad).error.empty(), "file size exceeds memory size");
	bad = Elf(true);
	Put(bad, 80, std::numeric_limits<uint64_t>::max(), 8);
	Check(!ProbeGuestExecutable(bad).error.empty(), "segment virtual address overflow");
	bad         = Self(false);
	bad[64 + 8] = 2;
	Check(!ProbeGuestExecutable(bad).error.empty(), "SELF and ELF generation conflict");
	bad = Self(false);
	Put(bad, 40, std::numeric_limits<uint64_t>::max(), 8);
	Check(!ProbeGuestExecutable(bad).error.empty(), "SELF payload offset overflow");
	bad = Self(false);
	Put(bad, 32, 0x100800, 8);
	Check(!ProbeGuestExecutable(bad).error.empty(), "SELF payload program index");
	bad = Self(false);
	Put(bad, 32, 0x808, 8);
	Check(!ProbeGuestExecutable(bad).error.empty(), "compressed SELF rejected");
	Check(!ProbeGuestExecutable(1024, [](uint64_t, std::span<uint8_t>) { return false; })
	           .error.empty(),
	      "read failure");
	Check(!ResolveGuestProfile({GuestPlatform::Unknown, "bad header"}, GuestPlatform::PS5)
	           .error.empty(),
	      "override does not bypass invalid header");
	Check(!GuestLaunchError({}).empty(), "unknown platform cannot start");
	Check(ParseGuestPlatform("auto") == GuestPlatform::Unknown &&
	          ParseGuestPlatform("ps4") == GuestPlatform::PS4 &&
	          ParseGuestPlatform("ps5") == GuestPlatform::PS5 && !ParseGuestPlatform("ps3"),
	      "selection parsing");
	const auto sfo      = Sfo();
	const auto metadata = ReadSfoMetadata(sfo);
	Check(metadata.error.empty() && metadata.strings.at("TITLE") == "PS4 synthetic test" &&
	          metadata.strings.at("TITLE_ID") == "TEST00001" &&
	          metadata.strings.at("APP_VER") == "01.00",
	      "PS4 metadata fields");
	for (size_t size = 0; size < sfo.size(); ++size)
		Check(!ReadSfoMetadata(std::span(sfo).first(size)).error.empty(), "truncated SFO");
	bad = sfo;
	Put(bad, 8, 0xffffffff, 4);
	Check(!ReadSfoMetadata(bad).error.empty(), "SFO key table bounds");
	bad = sfo;
	Put(bad, 32, 0xffffffff, 4);
	Check(!ReadSfoMetadata(bad).error.empty(), "SFO value bounds");
	bad        = sfo;
	bad.back() = 1;
	Check(!ReadSfoMetadata(bad).error.empty(), "SFO string terminator");
	bad = sfo;
	Put(bad, 22, 0x404, 2);
	Check(!ReadSfoMetadata(bad).error.empty(), "SFO integer width");
	bad     = sfo;
	bad[96] = 0xff;
	Check(!ReadSfoMetadata(bad).error.empty(), "invalid UTF-8 in SFO cannot reach UI conversion");
	for (size_t i = 0; i < sfo.size(); ++i) {
		bad = sfo;
		bad[i] ^= 0xff;
		(void)ReadSfoMetadata(bad);
	}
	std::puts("GuestPlatformTests: PASS (ELF/SELF, bounds, conflicts, override, launch gate, SFO)");
}
