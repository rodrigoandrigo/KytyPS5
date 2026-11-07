#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Common {

enum class GuestPlatform { Unknown, PS4, PS5 };
const char*                  GuestPlatformName(GuestPlatform platform);
std::optional<GuestPlatform> ParseGuestPlatform(std::string_view value);

struct GuestDetection {
	GuestPlatform platform = GuestPlatform::Unknown;
	std::string   error;
};

using GuestRead = std::function<bool(uint64_t, std::span<uint8_t>)>;
GuestDetection ProbeGuestExecutable(uint64_t size, const GuestRead& read);
GuestDetection ProbeGuestExecutable(std::span<const uint8_t> image);

struct SfoMetadata {
	std::map<std::string, std::string> strings;
	std::string                        error;
};
SfoMetadata ReadSfoMetadata(std::span<const uint8_t> bytes);

struct GuestGameInfo {
	GuestDetection detection;
	std::string    title;
	std::string    title_id;
	std::string    version;
};

// root is a folder or a Common::MakeArchivePath virtual archive root.
GuestGameInfo  InspectGuestGame(const std::filesystem::path& root,
                                const std::filesystem::path& executable = "eboot.bin");
GuestDetection ResolveGuestProfile(const GuestDetection& detected, GuestPlatform requested);
std::string    GuestLaunchError(const GuestDetection& profile);

} // namespace Common
