#include "catalog/search.h"
#include "check.h"

namespace {
  etal::AppEntry app(std::string id, std::string name, std::string summary, etal::InstallationKind kind = etal::InstallationKind::User) {
    etal::AppEntry entry;
    entry.id = std::move(id);
    entry.installation = kind;
    entry.name = std::move(name);
    entry.summary = std::move(summary);
    return entry;
  }
} // namespace

int main() {
  std::vector<etal::AppEntry> entries{
      app("org.example.Paint", "Paint", "Draw pictures"),
      app("org.example.PaintShop", "PaintShop", "Edit photos"),
      app("org.example.Notes", "Notes", "A painter's notebook"),
      app("org.example.Video", "Video", "Watch films"),
      app("org.example.Notes", "Notes (system)", "System copy", etal::InstallationKind::System),
  };
  entries[3].keywords = {"paint"};
  entries[1].verified = true;
  entries[1].freeLicense = true;
  entries[0].categories = {"Graphics"};
  const etal::Catalog catalog(std::move(entries));

  // Exact name first, then a name that starts with it, then a keyword, then the summary.
  const auto paint = etal::search(catalog, "paint", {});
  CHECK(paint.size() == 4);
  CHECK(paint[0] == 0);
  CHECK(paint[1] == 1);
  CHECK(paint[2] == 3);
  CHECK(paint[3] == 2);

  // Every word must match.
  CHECK(etal::search(catalog, "paint photos", {}).size() == 1);
  CHECK(etal::search(catalog, "PAINT  Photos", {}).front() == 1);
  CHECK(etal::search(catalog, "nothing", {}).empty());

  // One entry per app, the user installation's.
  const auto all = etal::search(catalog, "", {});
  CHECK(all.size() == 4);
  for (const auto index : all) {
    CHECK(catalog.entries()[index].installation == etal::InstallationKind::User);
  }
  // Without a query: by name.
  CHECK(catalog.entries()[all[0]].name == "Notes");

  CHECK(etal::search(catalog, "", {.onlyVerified = true, .onlyFree = false, .category = {}}).size() == 1);
  CHECK(etal::search(catalog, "", {.onlyVerified = false, .onlyFree = true, .category = {}}).size() == 1);
  CHECK(etal::search(catalog, "", {.onlyVerified = false, .onlyFree = false, .category = "Graphics"}).size() == 1);
  CHECK(catalog.find("org.example.Notes").size() == 2);
  CHECK(catalog.find("org.example.Notes").front()->installation == etal::InstallationKind::User);
  return 0;
}
