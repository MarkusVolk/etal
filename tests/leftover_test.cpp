#include "check.h"
#include "flatpak/installations.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

int main() {
  namespace fs = std::filesystem;
  const fs::path home = fs::temp_directory_path() / ("etal-leftover-test-" + std::to_string(::getpid()));
  const fs::path apps = home / ".var" / "app";
  fs::create_directories(apps / "org.example.Gone" / "config");
  fs::create_directories(apps / "org.example.Kept");
  std::ofstream(apps / "org.example.Gone" / "config" / "settings") << std::string(1000, 'x');
  std::ofstream(apps / "org.example.Kept" / "data") << "y";
  setenv("HOME", home.c_str(), 1);

  etal::InstalledApp kept;
  kept.id = "org.example.Kept";
  const auto found = etal::leftoverData({kept});
  CHECK(found.size() == 1);
  CHECK(found[0].id == "org.example.Gone");
  CHECK(found[0].size == 1000);

  // Nothing outside ~/.var/app, and nothing by a path.
  etal::deleteLeftoverData("../app/org.example.Kept");
  etal::deleteLeftoverData("");
  etal::deleteLeftoverData("..");
  CHECK(fs::exists(apps / "org.example.Kept" / "data"));
  etal::deleteLeftoverData("org.example.Gone");
  CHECK(!fs::exists(apps / "org.example.Gone"));
  CHECK(etal::leftoverData({kept}).empty());
  CHECK(fs::exists(apps / "org.example.Kept" / "data"));

  fs::remove_all(home);
  return 0;
}
