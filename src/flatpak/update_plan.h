#pragma once

#include "flatpak/transactions.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace etal {
  // Which of the waiting updates may be installed without asking: those whose new version wants
  // no more permissions than the installed one.
  struct UpdatePlan {
    std::vector<UpdateItem> automatic;
    std::vector<UpdateItem> manual;
  };
  UpdatePlan planUpdates(const std::vector<UpdateItem>& updates, bool automaticUpdates);

  struct CheckOutcome {
    std::vector<UpdateItem> updated;
    std::vector<UpdateItem> pending;
    std::size_t removedRuntimes = 0;
    std::vector<std::string> problems;
  };

  struct Notice {
    std::string summary;
    std::string body;
  };
  // What to tell the user about a check, if anything: updated runtimes alone are not worth a
  // notification. `nameOf` gives the app's name for its id.
  std::optional<Notice> noticeFor(const CheckOutcome& outcome, const std::function<std::string(const std::string& id)>& nameOf);
} // namespace etal
