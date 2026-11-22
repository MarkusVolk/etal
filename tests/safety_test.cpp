#include "catalog/safety.h"
#include "check.h"

#include <algorithm>

namespace {
  bool lists(const etal::Safety& safety, std::string_view title) {
    return std::ranges::any_of(safety.permissions, [title](const etal::Permission& p) { return p.title == title; });
  }
} // namespace

int main() {
  using etal::SafetyLevel;
  // Nothing asked for.
  auto none = etal::assessPermissions("[Application]\nname=org.example.App\n");
  CHECK(none.level == SafetyLevel::Safe);
  CHECK(none.permissions.size() == 1 && none.permissions[0].title == "No permissions");

  // A typical app: Wayland, network, sound, downloads.
  auto typical = etal::assessPermissions(
      "[Context]\nshared=network;ipc;\nsockets=wayland;fallback-x11;pulseaudio;\ndevices=dri;\nfilesystems=xdg-download;\n"
  );
  CHECK(typical.level == SafetyLevel::Safe);
  CHECK(lists(typical, "Network access"));
  CHECK(lists(typical, "Download folder"));
  CHECK(!lists(typical, "Legacy windowing system (X11)"));

  // X11 alone and the home folder read-only are worth a warning.
  auto x11 = etal::assessPermissions("[Context]\nsockets=x11;\nfilesystems=home:ro;\n");
  CHECK(x11.level == SafetyLevel::Unsafe);
  CHECK(lists(x11, "Legacy windowing system (X11)"));
  CHECK(lists(x11, "Home folder (read-only)"));

  // The whole file system, the session bus and the flatpak portal's spawn amount to no sandbox.
  auto host = etal::assessPermissions("[Context]\nfilesystems=host;!home;\n");
  CHECK(host.level == SafetyLevel::Privileged);
  CHECK(host.permissions.front().title == "Full file system access");
  CHECK(!lists(host, "Home folder"));
  auto spawn = etal::assessPermissions("[Context]\nsockets=wayland;\n\n[Session Bus Policy]\norg.freedesktop.Flatpak=talk\n");
  CHECK(spawn.level == SafetyLevel::Privileged);
  CHECK(lists(spawn, "Can run programs outside the sandbox"));
  CHECK(etal::assessPermissions("[Context]\nsockets=session-bus;\n").level == SafetyLevel::Privileged);
  CHECK(etal::assessPermissions("[Context]\nfilesystems=home;\n").level == SafetyLevel::Privileged);

  // Devices.
  CHECK(lists(etal::assessPermissions("[Context]\ndevices=all;\n"), "All devices, such as webcams and USB"));
  // The weightiest first.
  auto mixed = etal::assessPermissions("[Context]\nshared=network;\nsockets=x11;\nfilesystems=host;\n");
  CHECK(mixed.permissions.front().level == SafetyLevel::Privileged);
  CHECK(mixed.permissions.back().level == SafetyLevel::Safe);
  // Garbage is no permission.
  CHECK(etal::assessPermissions("not a key file").level == SafetyLevel::Safe);
  return 0;
}
