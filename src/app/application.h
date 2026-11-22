#pragma once

#include <string>
#include <vector>

namespace etal {
  struct Launch {
    // .flatpakref, .flatpakrepo or .flatpak files to open.
    std::vector<std::string> files;
    // A named action to run once the window is there, with its parameter.
    std::string action;
    std::string parameter;
  };

  // Runs the store until its window closes. A second launch hands its files and action to the first.
  int runApplication(const Launch& launch);
} // namespace etal
