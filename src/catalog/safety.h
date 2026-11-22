#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace etal {
  enum class SafetyLevel : std::uint8_t {
    // Nothing that reaches beyond the sandbox in a way that matters.
    Safe,
    // Access a careless or hostile app could misuse: the home folder, X11, devices.
    Unsafe,
    // Access that amounts to leaving the sandbox: the whole file system, the session bus.
    Privileged,
  };

  struct Permission {
    std::string glyph;
    std::string title;
    SafetyLevel level = SafetyLevel::Safe;
  };

  struct Safety {
    SafetyLevel level = SafetyLevel::Safe;
    // What the app asks for, the weightiest first; one "No permissions" row when it asks for nothing.
    std::vector<Permission> permissions;
  };

  // Reads the [Context] and bus policy groups of a flatpak metadata file.
  Safety assessPermissions(std::string_view metadata);

  std::string_view safetyTitle(SafetyLevel level);
} // namespace etal
