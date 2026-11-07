#include "GuestPlatformTestFixture.h"
#include "common/archive.h"
#include "common/guestPlatform.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace {
void Check(bool value, const char* message) {
	if (!value) {
		std::fprintf(stderr, "GuestPlatformFileTests: %s\n", message);
		std::abort();
	}
}
void Write(const std::filesystem::path& path, std::span<const uint8_t> data) {
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char*>(data.data()),
	          static_cast<std::streamsize>(data.size()));
	Check(out.good(), "write fixture");
}
void Text(const std::filesystem::path& path, std::string_view text) {
	Write(path, {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}
} // namespace
int main() {
	using namespace Common;
	using namespace GuestPlatformTests;
	const auto temp = std::filesystem::temp_directory_path() /
	                  ("kyty_platform_" +
	                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(temp / "ps4/sce_sys");
	std::filesystem::create_directories(temp / "ps5/sce_sys");
	Write(temp / "ps4/eboot.bin", Elf(false));
	Write(temp / "ps4/sce_sys/param.sfo", Sfo());
	Write(temp / "ps5/eboot.bin", Elf(true));
	Text(
	    temp / "ps5/sce_sys/param.json",
	    R"({"titleId":"PPSATEST01","appVersion":"01.02","localizedParameters":{"defaultLanguage":"pt-BR","pt-BR":{"titleName":"PS5 synthetic test"}}})");
	const auto verify = [](const std::filesystem::path& root, bool ps5) {
		const auto info = InspectGuestGame(root);
		Check(info.detection.error.empty(), "inspect valid game");
		Check(info.detection.platform == (ps5 ? GuestPlatform::PS5 : GuestPlatform::PS4),
		      "file-backed platform");
		Check(info.title == (ps5 ? "PS5 synthetic test" : "PS4 synthetic test"), "title");
		Check(info.title_id == (ps5 ? "PPSATEST01" : "TEST00001"), "title ID");
		Check(info.version == (ps5 ? "01.02" : "01.00"), "version");
		Check(
		    GuestLaunchError(ResolveGuestProfile(info.detection, GuestPlatform::Unknown)).empty() ==
		        ps5,
		    "launch gate");
	};
	for (bool ps5: {false, true}) {
		const auto folder = temp / (ps5 ? "ps5" : "ps4");
		verify(folder, ps5);
		const auto archive = temp / (ps5 ? "ps5.zar" : "ps4.zar");
		Check(PackArchive(folder, archive, [](uint64_t, uint64_t) { return true; }),
		      "pack synthetic game");
		verify(MakeArchivePath(archive), ps5);
	}
	Text(temp / "ps4/sce_sys/param.json", "{}");
	Check(!InspectGuestGame(temp / "ps4").detection.error.empty(), "conflicting metadata");
	std::filesystem::remove(temp / "ps4/sce_sys/param.json");
	Write(temp / "ps4/eboot.bin", Elf(true));
	Check(!InspectGuestGame(temp / "ps4").detection.error.empty(),
	      "PS5 executable with PS4 metadata");
	Write(temp / "ps4/eboot.bin", std::span<const uint8_t> {});
	Check(!InspectGuestGame(temp / "ps4").detection.error.empty(),
	      "metadata cannot rescue invalid executable");
	Check(!InspectGuestGame(temp / "missing").detection.error.empty(), "missing executable");
	Check(!InspectGuestGame(temp / "ps5", "../ps4/eboot.bin").detection.error.empty(),
	      "reject relative path escape");
	Check(!InspectGuestGame(temp / "ps5", temp / "ps4/eboot.bin").detection.error.empty(),
	      "reject absolute executable path");
	Text(temp / "ps5/sce_sys/param.json", "{");
	Check(!InspectGuestGame(temp / "ps5").detection.error.empty(), "invalid JSON metadata");
	std::filesystem::remove(temp / "ps5/sce_sys/param.json");
	Check(InspectGuestGame(temp / "ps5").detection.error.empty(),
	      "metadata optional for valid ELF");
	std::filesystem::remove_all(temp);
	std::puts("GuestPlatformFileTests: PASS (PS4/PS5 folders and ZArchive, metadata, conflicts, "
	          "launch gate)");
}
