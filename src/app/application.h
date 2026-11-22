#pragma once

#include <string>
#include <vector>

namespace etal {
  // Runs the store until its window closes; `files` are .flatpakref, .flatpakrepo or .flatpak files to open.
  int runApplication(const std::vector<std::string>& files);
} // namespace etal
