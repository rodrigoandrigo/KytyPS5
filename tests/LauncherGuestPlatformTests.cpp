#include "configuration.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

namespace {
void Check(bool value, const char* message) {
	if (!value) {
		std::fprintf(stderr, "LauncherGuestPlatformTests: %s\n", message);
		std::abort();
	}
}
} // namespace
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir    folder;
	Check(folder.isValid(), "temporary settings folder");
	Configuration automatic;
	Check(automatic.guest_platform == "auto", "default selection stays automatic");
	Configuration source;
	source.guest_platform = "ps4";
	source.platform       = Common::GuestPlatform::PS4;
	source.platform_error = "not executable yet";
	Configuration copy;
	copy.CopyGameInfoFrom(source);
	Check(copy.platform == Common::GuestPlatform::PS4 &&
	          copy.platform_error == source.platform_error,
	      "detected platform survives metadata refresh");
	Check(copy.guest_platform == "auto", "metadata does not overwrite requested platform");
	copy.CopyEmulatorSettingsFrom(source);
	Check(copy.guest_platform == "ps4", "per-game override survives configuration composition");
	QSettings settings(folder.filePath("game.ini"), QSettings::IniFormat);
	source.WriteSettings(&settings);
	settings.sync();
	Check(settings.status() == QSettings::NoError, "persist settings file");
	QSettings     reopened(settings.fileName(), QSettings::IniFormat);
	Configuration loaded;
	loaded.ReadSettings(&reopened);
	Check(loaded.guest_platform == "ps4",
	      "requested platform round-trips through existing settings");
	QSettings legacy(folder.filePath("legacy.ini"), QSettings::IniFormat);
	loaded.ReadSettings(&legacy);
	Check(loaded.guest_platform == "auto", "legacy settings default to auto");
	QString       error;
	Configuration configured;
	Check(configured.SetGameSettings({{"guest_platform", "ps5"}}, error),
	      "valid imported override");
	Check(configured.guest_platform == "ps5", "imported value propagated");
	Check(!configured.SetGameSettings({{"guest_platform", "ps3"}}, error),
	      "invalid imported override rejected");
	std::puts("LauncherGuestPlatformTests: PASS (default, metadata, per-game copy, settings "
	          "round-trip and import)");
}
