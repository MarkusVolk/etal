#include "app/application.h"
#include "app/background.h"
#include "core/application_identity.h"
#include "etal_build_info.h"

#include <print>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char* argv[]) {
  setApplicationIdentity({.name = "etal", .envPrefix = "ETAL"});
  etal::Launch launch;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--check-updates") {
      return etal::runBackgroundCheck();
    }
    if (argument == "--version") {
      std::println("etal {}", ETAL_VERSION);
      return 0;
    }
    if (argument == "--help") {
      std::println(
          "Usage: etal [FILE...]\n"
          "       etal --action NAME [PARAMETER]\n"
          "       etal --check-updates\n\n"
          "FILE is a .flatpakref, .flatpak or .flatpakrepo file to install or add.\n"
          "--action runs a named action, in the running Etal when there is one; the README lists them.\n"
          "--check-updates looks for updates without a window, as the etal-update timer does."
      );
      return 0;
    }
    if (argument == "--action" && index + 1 < argc) {
      launch.action = argv[++index];
      if (index + 1 < argc && argv[index + 1][0] != '-') {
        launch.parameter = argv[++index];
      }
      continue;
    }
    launch.files.emplace_back(argument);
  }
  return etal::runApplication(launch);
}
