#include "catalog/catalog.h"
#include "check.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

int main() {
  const char* fixtures = std::getenv("ETAL_TEST_FIXTURES");
  CHECK(fixtures != nullptr);
  // libappstream keeps a cache; a fresh one per run keeps the test from reading an old fixture.
  const auto cache = std::filesystem::temp_directory_path() / ("etal-catalog-test-" + std::to_string(::getpid()));
  setenv("XDG_CACHE_HOME", cache.c_str(), 1);
  setenv("LANGUAGE", "C", 1);

  const auto catalog = etal::Catalog::load({{.remote = "flathub", .directory = std::filesystem::path(fixtures) / "flathub"}});
  std::filesystem::remove_all(cache);
  CHECK(catalog.has_value());
  // Runtimes and components without a flatpak bundle are left out.
  CHECK(catalog->entries().size() == 2);

  const auto paint = catalog->find("org.example.Paint");
  CHECK(paint.size() == 1);
  const etal::AppEntry& app = *paint.front();
  CHECK(app.name == "Paint & Draw");
  CHECK(app.remote == "flathub");
  CHECK(app.ref == "app/org.example.Paint/x86_64/stable");
  CHECK(app.developer == "Example Team");
  CHECK(app.verified);
  CHECK(app.freeLicense);
  CHECK(app.description == "Paint is a <i>simple</i> drawing app.\n\n• Brushes &lt;3\n• Layers");
  CHECK(app.categories.size() == 2);
  CHECK(app.keywords.size() == 2);
  CHECK(app.iconFile.ends_with("icons/flatpak/64x64/org.example.Paint.png"));
  CHECK(app.iconUrl == "https://example.org/paint-128.png");
  CHECK(app.screenshots.size() == 1);
  CHECK(app.screenshots[0].imageUrl == "https://example.org/shot.png");
  CHECK(app.screenshots[0].thumbnailUrl == "https://example.org/shot-752.png");
  CHECK(app.screenshots[0].caption == "The canvas");
  CHECK(app.links.homepage == "https://example.org/paint");
  CHECK(app.links.donation == "https://example.org/donate");
  CHECK(app.releases.size() == 2);
  CHECK(app.releases[0].version == "2.0");
  CHECK(app.releases[0].notes == "New brushes.");
  CHECK(app.hasContentRating);
  CHECK(app.minimumAge > 0);

  const etal::AppEntry& notes = *catalog->find("org.example.Notes").front();
  CHECK(!notes.verified);
  CHECK(!notes.freeLicense);
  CHECK(notes.iconFile.empty());
  CHECK(!notes.hasContentRating);
  return 0;
}
