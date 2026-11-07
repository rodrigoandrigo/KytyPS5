#include "gameLibrary.h"

#include <windows.h>

#include "common/archive.h"
#include "fileAccess.h"
#include "gameSource.h"
#include "log.h"
#include "settings.h"

#include <algorithm>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <winrt/base.h>

namespace Kyty::Uwp {

namespace {

std::string JsonString(const nlohmann::json& object, const char* key) {
	const auto it = object.find(key);
	return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

void ScanFolder(const std::filesystem::path& folder, int depth, std::vector<GameEntry>& games) {
	if (IsFile(folder / L"eboot.bin")) {
		games.push_back(ReadGame(folder));
		return;
	}
	// .zar archives: a game dump the emulator reads in place
	for (const auto& archive: ArchivesIn(folder)) {
		if (GameFileExists(archive / L"eboot.bin")) {
			games.push_back(ReadGame(archive));
		} else {
			Log("archive %ls has no eboot.bin at its root\n", archive.c_str());
		}
	}
	if (depth == 0) {
		return;
	}
	for (const auto& subfolder: Subfolders(folder)) {
		ScanFolder(subfolder, depth - 1, games);
	}
}

std::filesystem::path LibraryPath() {
	return LocalStateFolder() / L"library.json";
}

void SaveLibrary(const std::vector<GameEntry>& games) {
	auto list = nlohmann::json::array();
	for (const auto& game: games) {
		list.push_back({{"title_id", game.title_id},
		                {"name", game.name},
		                {"version", game.version},
		                {"platform", Common::GuestPlatformName(game.platform)},
		                {"platform_error", game.platform_error},
		                {"folder", winrt::to_string(game.folder.wstring())}});
	}
	const auto text = nlohmann::json {{"games", list}}.dump(2);
	if (FILE* file = _wfopen(LibraryPath().c_str(), L"wb"); file != nullptr) {
		fwrite(text.data(), 1, text.size(), file);
		fclose(file);
	}
}

} // namespace

GameEntry ReadGame(const std::filesystem::path& folder) {
	GameEntry game;
	game.folder = folder;
	game.name   = winrt::to_string((IsArchivePath(folder) ? folder.stem() : folder.filename()).wstring());
	const auto root     = IsArchivePath(folder) ? Common::MakeArchivePath(folder) : folder;
	const auto info     = Common::InspectGuestGame(root);
	game.platform       = info.detection.platform;
	game.platform_error = info.detection.error;
	game.title_id       = info.title_id;
	game.version        = info.version;
	if (!info.title.empty()) game.name = info.title;

	return game;
}

std::vector<GameEntry> ScanGames() {
	std::vector<GameEntry> games;
	for (const auto& folder: GameFolders()) {
		const auto before = games.size();
		ScanFolder(folder, 2, games);
		Log("game folder %ls: %zu games\n", folder.c_str(), games.size() - before);
	}
	std::sort(games.begin(), games.end(),
	          [](const GameEntry& a, const GameEntry& b) { return a.name < b.name; });
	SaveLibrary(games);
	return games;
}

std::vector<GameEntry> LoadGameLibrary() {
	std::vector<GameEntry> games;
	const auto             text = ReadTextFile(LibraryPath());
	if (!text) {
		return games;
	}
	const auto library = nlohmann::json::parse(*text, nullptr, false);
	if (library.is_discarded() || !library.contains("games") || !library["games"].is_array()) {
		return games;
	}
	for (const auto& entry: library["games"]) {
		if (!entry.is_object()) {
			continue;
		}
		GameEntry game;
		game.title_id = JsonString(entry, "title_id");
		game.name     = JsonString(entry, "name");
		game.version  = JsonString(entry, "version");
		game.folder   = std::filesystem::path(winrt::to_hstring(JsonString(entry, "folder")).c_str());
		if (!entry.contains("platform")) {
			const auto refreshed = ReadGame(game.folder);
			game.platform        = refreshed.platform;
			game.platform_error  = refreshed.platform_error;
			if (game.name.empty()) game.name = refreshed.name;
			if (game.title_id.empty()) game.title_id = refreshed.title_id;
			if (game.version.empty()) game.version = refreshed.version;
		} else {
			game.platform = Common::ParseGuestPlatform(JsonString(entry, "platform"))
			                    .value_or(Common::GuestPlatform::Unknown);
			game.platform_error = JsonString(entry, "platform_error");
		}
		games.push_back(std::move(game));
	}
	return games;
}

std::optional<GameEntry> FindGame(const std::string& title_id) {
	const auto find = [&](const std::vector<GameEntry>& games) -> std::optional<GameEntry> {
		const auto it = std::find_if(games.begin(), games.end(), [&](const GameEntry& game) {
			return _stricmp(game.title_id.c_str(), title_id.c_str()) == 0;
		});
		return it != games.end() ? std::optional(*it) : std::nullopt;
	};
	if (auto game = find(LoadGameLibrary())) {
		return game;
	}
	return find(ScanGames());
}

} // namespace Kyty::Uwp
