#include "flatpak/update_plan.h"

#include <algorithm>
#include <format>

namespace {
  std::string listNames(const std::vector<std::string>& names) {
    constexpr std::size_t kShown = 4;
    std::string out;
    for (std::size_t i = 0; i < names.size() && i < kShown; ++i) {
      out += (i ? ", " : "") + names[i];
    }
    if (names.size() > kShown) {
      out += std::format(" and {} more", names.size() - kShown);
    }
    return out;
  }

  std::string count(std::size_t n, const char* one, const char* many) { return n == 1 ? std::string(one) : std::format("{} {}", n, many); }
} // namespace

etal::UpdatePlan etal::planUpdates(const std::vector<UpdateItem>& updates, bool automaticUpdates) {
  UpdatePlan plan;
  for (const auto& item : updates) {
    (automaticUpdates && item.newPermissions.empty() ? plan.automatic : plan.manual).push_back(item);
  }
  return plan;
}

std::optional<etal::Notice>
etal::noticeFor(const CheckOutcome& outcome, const std::function<std::string(const std::string& id)>& nameOf) {
  const auto apps = [&nameOf](const std::vector<UpdateItem>& items) {
    std::vector<std::string> names;
    for (const auto& item : items) {
      if (!item.runtime) {
        names.push_back(nameOf(item.id));
      }
    }
    std::ranges::sort(names);
    return names;
  };
  const auto updated = apps(outcome.updated);
  const auto pending = apps(outcome.pending);
  const bool pendingRuntimes = pending.empty() && !outcome.pending.empty();
  const bool asks = std::ranges::any_of(outcome.pending, [](const UpdateItem& item) { return !item.newPermissions.empty(); });
  if (updated.empty() && pending.empty() && !pendingRuntimes) {
    return std::nullopt;
  }
  Notice notice;
  if (!updated.empty() && pending.empty() && !pendingRuntimes) {
    notice.summary = std::format("{} updated", count(updated.size(), "1 app", "apps"));
    notice.body = listNames(updated);
    return notice;
  }
  const std::string waiting = pendingRuntimes ? std::string("System components can be updated")
                                              : std::format("{} available", count(pending.size(), "1 update", "updates"));
  notice.summary = updated.empty() ? waiting : std::format("{} updated, {}", count(updated.size(), "1 app", "apps"), pendingRuntimes ? "more wait" : waiting);
  notice.body = pendingRuntimes ? std::string("Runtimes and drivers the apps share") : listNames(pending);
  if (asks) {
    notice.body += "\nSome ask for new permissions.";
  }
  return notice;
}
