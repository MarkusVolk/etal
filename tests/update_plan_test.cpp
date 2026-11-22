#include "check.h"
#include "flatpak/update_plan.h"

namespace {
  etal::UpdateItem item(std::string id, bool runtime = false, std::vector<std::string> permissions = {}) {
    etal::UpdateItem out;
    out.id = std::move(id);
    out.runtime = runtime;
    out.newPermissions = std::move(permissions);
    return out;
  }
  std::string name(const std::string& id) { return id.substr(id.rfind('.') + 1); }
} // namespace

int main() {
  const std::vector<etal::UpdateItem> updates{
      item("org.a.Paint"), item("org.a.Notes", false, {"Home folder"}), item("org.a.Platform", true)
  };
  // Off: everything waits. On: all but what asks for more.
  CHECK(etal::planUpdates(updates, false).automatic.empty());
  CHECK(etal::planUpdates(updates, false).manual.size() == 3);
  const auto plan = etal::planUpdates(updates, true);
  CHECK(plan.automatic.size() == 2);
  CHECK(plan.manual.size() == 1 && plan.manual[0].id == "org.a.Notes");

  // Nothing happened, or only runtimes were updated: no notification.
  CHECK(!etal::noticeFor({}, name).has_value());
  etal::CheckOutcome quiet;
  quiet.updated = {item("org.a.Platform", true)};
  CHECK(!etal::noticeFor(quiet, name).has_value());

  etal::CheckOutcome updated;
  updated.updated = {item("org.a.Paint"), item("org.a.Platform", true)};
  auto notice = etal::noticeFor(updated, name);
  CHECK(notice && notice->summary == "1 app updated" && notice->body == "Paint");

  etal::CheckOutcome waiting;
  waiting.pending = {item("org.a.Paint"), item("org.a.Notes", false, {"Home folder"})};
  notice = etal::noticeFor(waiting, name);
  CHECK(notice && notice->summary == "2 updates available");
  CHECK(notice->body == "Notes, Paint\nSome ask for new permissions.");

  etal::CheckOutcome both;
  both.updated = {item("org.a.Paint"), item("org.a.Draw")};
  both.pending = {item("org.a.Notes", false, {"Home folder"})};
  notice = etal::noticeFor(both, name);
  CHECK(notice && notice->summary == "2 apps updated, 1 update available");

  etal::CheckOutcome runtimes;
  runtimes.pending = {item("org.a.Platform", true)};
  notice = etal::noticeFor(runtimes, name);
  CHECK(notice && notice->summary == "System components can be updated");

  etal::CheckOutcome many;
  for (const char* id : {"a.A", "a.B", "a.C", "a.D", "a.E", "a.F"}) {
    many.updated.push_back(item(id));
  }
  notice = etal::noticeFor(many, name);
  CHECK(notice && notice->summary == "6 apps updated" && notice->body == "A, B, C, D and 2 more");
  return 0;
}
