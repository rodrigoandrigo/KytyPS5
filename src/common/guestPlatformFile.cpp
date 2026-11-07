#include "common/file.h"
#include "common/guestPlatform.h"

#include <nlohmann/json.hpp>
#include <vector>

namespace Common {
namespace {
std::vector<uint8_t> ReadMetadata(const std::filesystem::path& path) {
	File file(path, File::Mode::Read);
	if (file.IsInvalid() || file.Size() > 1024 * 1024) return {};
	std::vector<uint8_t> data(static_cast<size_t>(file.Size()));
	uint32_t             read = 0;
	file.Read(data.data(), static_cast<uint32_t>(data.size()), &read);
	return read == data.size() ? data : std::vector<uint8_t> {};
}
std::string JsonString(const nlohmann::json& json, const char* key) {
	const auto it = json.find(key);
	return it != json.end() && it->is_string() ? it->get<std::string>() : std::string {};
}
} // namespace

GuestGameInfo InspectGuestGame(const std::filesystem::path& root,
                               const std::filesystem::path& executable) {
	GuestGameInfo info;
	const auto    relative = executable.lexically_normal();
	if (relative.empty() || relative.has_root_path() || *relative.begin() == "..") {
		info.detection.error = "The executable must be inside the game folder.";
		return info;
	}
	File file(root / relative, File::Mode::Read);
	if (file.IsInvalid()) {
		info.detection.error = "Cannot read the game executable.";
	} else {
		info.detection =
		    ProbeGuestExecutable(file.Size(), [&file](uint64_t offset, std::span<uint8_t> target) {
			    if (!file.Seek(offset)) return false;
			    uint32_t read = 0;
			    file.Read(target.data(), static_cast<uint32_t>(target.size()), &read);
			    return read == target.size();
		    });
	}
	const auto json_path = root / "sce_sys" / "param.json";
	const auto sfo_path  = root / "sce_sys" / "param.sfo";
	const bool has_json = File::IsFileExisting(json_path), has_sfo = File::IsFileExisting(sfo_path);
	const auto error = [&info](const char* text) {
		if (info.detection.error.empty()) info.detection.error = text;
	};
	if (has_json && has_sfo)
		error("Conflicting metadata: both param.json and param.sfo are present.");
	if (has_sfo) {
		const auto metadata = ReadSfoMetadata(ReadMetadata(sfo_path));
		if (!metadata.error.empty()) error(metadata.error.c_str());
		const auto get = [&metadata](const char* key) {
			const auto it = metadata.strings.find(key);
			return it == metadata.strings.end() ? std::string {} : it->second;
		};
		info.title    = get("TITLE");
		info.title_id = get("TITLE_ID");
		info.version  = get("APP_VER");
		if (info.detection.platform == GuestPlatform::PS5)
			error("param.sfo conflicts with the PS5 executable.");
	}
	if (has_json) {
		const auto data = ReadMetadata(json_path);
		const auto json = nlohmann::json::parse(data.begin(), data.end(), nullptr, false);
		if (!json.is_object()) {
			error("Invalid or unreadable param.json.");
		} else {
			info.title_id = JsonString(json, "titleId");
			info.version  = JsonString(json, "appVersion");
			if (info.version.empty()) info.version = JsonString(json, "contentVersion");
			if (const auto localized = json.find("localizedParameters");
			    localized != json.end() && localized->is_object()) {
				for (const auto& language:
				     {JsonString(*localized, "defaultLanguage"), std::string("en-US")}) {
					const auto entry = localized->find(language);
					if (entry != localized->end() && entry->is_object())
						info.title = JsonString(*entry, "titleName");
					if (!info.title.empty()) break;
				}
				if (info.title.empty())
					for (const auto& entry: *localized) {
						if (entry.is_object()) info.title = JsonString(entry, "titleName");
						if (!info.title.empty()) break;
					}
			}
		}
		if (info.detection.platform == GuestPlatform::PS4)
			error("param.json conflicts with the PS4 executable.");
	}
	return info;
}
} // namespace Common
