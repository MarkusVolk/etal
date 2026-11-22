#include "catalog/catalog.h"

#include "catalog/markup.h"

#include <algorithm>
#include <appstream.h>
#include <format>
#include <memory>

namespace {
  struct PoolDeleter {
    void operator()(AsPool* pool) const { g_object_unref(pool); }
  };
  struct BoxDeleter {
    void operator()(AsComponentBox* box) const { g_object_unref(box); }
  };

  std::string str(const char* text) { return text ? std::string(text) : std::string(); }

  std::vector<std::string> strings(GPtrArray* array) {
    std::vector<std::string> out;
    if (array) {
      for (guint i = 0; i < array->len; ++i) {
        out.emplace_back(static_cast<const char*>(g_ptr_array_index(array, i)));
      }
    }
    return out;
  }

  // The local file of the largest cached icon up to 128 pixels, and the URL of the largest remote one.
  void icons(AsComponent* component, etal::AppEntry& entry) {
    GPtrArray* icons = as_component_get_icons(component);
    guint bestLocal = 0, bestRemote = 0;
    for (guint i = 0; icons && i < icons->len; ++i) {
      auto* icon = static_cast<AsIcon*>(g_ptr_array_index(icons, i));
      const guint size = as_icon_get_width(icon) * std::max(1U, as_icon_get_scale(icon));
      const auto kind = as_icon_get_kind(icon);
      if (kind == AS_ICON_KIND_CACHED || kind == AS_ICON_KIND_LOCAL) {
        const char* file = as_icon_get_filename(icon);
        if (file && size <= 128 && size > bestLocal) {
          entry.iconFile = file;
          bestLocal = size;
        }
      } else if (kind == AS_ICON_KIND_REMOTE) {
        const char* url = as_icon_get_url(icon);
        if (url && size > bestRemote) {
          entry.iconUrl = url;
          bestRemote = size;
        }
      }
    }
  }

  void screenshots(AsComponent* component, etal::AppEntry& entry) {
    constexpr guint kThumbnailWidth = 624;
    GPtrArray* shots = as_component_get_screenshots_all(component);
    for (guint i = 0; shots && i < shots->len; ++i) {
      auto* shot = static_cast<AsScreenshot*>(g_ptr_array_index(shots, i));
      if (as_screenshot_get_media_kind(shot) != AS_SCREENSHOT_MEDIA_KIND_IMAGE) {
        continue;
      }
      etal::Screenshot screenshot;
      screenshot.caption = str(as_screenshot_get_caption(shot));
      // The full image is the source; the thumbnail is the smallest one still sharp at its size
      // in the window, or the largest there is.
      std::string largest;
      guint largestWidth = 0, thumbWidth = 0;
      GPtrArray* images = as_screenshot_get_images_all(shot);
      for (guint j = 0; images && j < images->len; ++j) {
        auto* image = static_cast<AsImage*>(g_ptr_array_index(images, j));
        const char* url = as_image_get_url(image);
        if (!url) {
          continue;
        }
        const guint width = as_image_get_width(image);
        if (as_image_get_kind(image) == AS_IMAGE_KIND_SOURCE) {
          screenshot.imageUrl = url;
          continue;
        }
        if (width > largestWidth) {
          largest = url;
          largestWidth = width;
        }
        if (width >= kThumbnailWidth && (thumbWidth == 0 || width < thumbWidth)) {
          screenshot.thumbnailUrl = url;
          thumbWidth = width;
        }
      }
      if (screenshot.thumbnailUrl.empty()) {
        screenshot.thumbnailUrl = largest;
      }
      if (screenshot.imageUrl.empty()) {
        screenshot.imageUrl = largest;
      }
      if (screenshot.thumbnailUrl.empty()) {
        screenshot.thumbnailUrl = screenshot.imageUrl;
      }
      if (!screenshot.imageUrl.empty()) {
        entry.screenshots.push_back(std::move(screenshot));
      }
    }
  }

  void releases(AsComponent* component, etal::AppEntry& entry) {
    AsReleaseList* list = as_component_get_releases_plain(component);
    if (!list) {
      return;
    }
    GPtrArray* releases = as_release_list_get_entries(list);
    for (guint i = 0; releases && i < releases->len; ++i) {
      auto* release = static_cast<AsRelease*>(g_ptr_array_index(releases, i));
      entry.releases.push_back({
          .version = str(as_release_get_version(release)),
          .timestamp = static_cast<std::int64_t>(as_release_get_timestamp(release)),
          .notes = etal::appstreamToMarkup(str(as_release_get_description(release))),
      });
    }
  }

  void contentRating(AsComponent* component, etal::AppEntry& entry) {
    GPtrArray* ratings = as_component_get_content_ratings(component);
    for (guint i = 0; ratings && i < ratings->len; ++i) {
      auto* rating = static_cast<AsContentRating*>(g_ptr_array_index(ratings, i));
      entry.hasContentRating = true;
      entry.minimumAge = std::max(entry.minimumAge, as_content_rating_get_minimum_age(rating));
    }
  }

  std::optional<etal::AppEntry> entryOf(AsComponent* component, const etal::CatalogSource& source) {
    const auto kind = as_component_get_kind(component);
    if (kind != AS_COMPONENT_KIND_DESKTOP_APP && kind != AS_COMPONENT_KIND_CONSOLE_APP) {
      return std::nullopt;
    }
    AsBundle* bundle = as_component_get_bundle(component, AS_BUNDLE_KIND_FLATPAK);
    if (!bundle || !as_bundle_get_id(bundle)) {
      return std::nullopt;
    }
    etal::AppEntry entry;
    entry.id = str(as_component_get_id(component));
    entry.ref = as_bundle_get_id(bundle);
    entry.remote = source.remote;
    entry.installation = source.installation;
    entry.name = str(as_component_get_name(component));
    entry.summary = str(as_component_get_summary(component));
    entry.description = etal::appstreamToMarkup(str(as_component_get_description(component)));
    if (entry.name.empty()) {
      entry.name = entry.id;
    }
    if (AsDeveloper* developer = as_component_get_developer(component)) {
      entry.developer = str(as_developer_get_name(developer));
    }
    entry.license = str(as_component_get_project_license(component));
    entry.freeLicense = !entry.license.empty() && as_license_is_free_license(entry.license.c_str());
    entry.verified = str(as_component_get_custom_value(component, "flathub::verification::verified")) == "true";
    entry.categories = strings(as_component_get_categories(component));
    entry.keywords = strings(as_component_get_keywords(component));
    icons(component, entry);
    screenshots(component, entry);
    releases(component, entry);
    contentRating(component, entry);
    entry.links = {
        .homepage = str(as_component_get_url(component, AS_URL_KIND_HOMEPAGE)),
        .bugtracker = str(as_component_get_url(component, AS_URL_KIND_BUGTRACKER)),
        .donation = str(as_component_get_url(component, AS_URL_KIND_DONATION)),
        .help = str(as_component_get_url(component, AS_URL_KIND_HELP)),
        .translate = str(as_component_get_url(component, AS_URL_KIND_TRANSLATE)),
        .vcs = str(as_component_get_url(component, AS_URL_KIND_VCS_BROWSER)),
    };
    return entry;
  }
} // namespace

etal::Catalog::Catalog(std::vector<AppEntry> entries) : m_entries(std::move(entries)) {
  for (std::size_t i = 0; i < m_entries.size(); ++i) {
    m_byId.emplace(m_entries[i].id, i);
  }
}

std::vector<const etal::AppEntry*> etal::Catalog::find(std::string_view id) const {
  std::vector<const AppEntry*> out;
  const auto [first, last] = m_byId.equal_range(std::string(id));
  for (auto it = first; it != last; ++it) {
    out.push_back(&m_entries[it->second]);
  }
  std::ranges::stable_sort(out, {}, [](const AppEntry* entry) { return entry->installation != InstallationKind::User; });
  return out;
}

std::expected<etal::Catalog, std::string> etal::Catalog::load(const std::vector<CatalogSource>& sources) {
  std::vector<AppEntry> entries;
  // One pool per source: the origin of a component is the remote, and only one pool knows which.
  for (const auto& source : sources) {
    std::unique_ptr<AsPool, PoolDeleter> pool(as_pool_new());
    as_pool_set_load_std_data_locations(pool.get(), FALSE);
    as_pool_set_flags(pool.get(), AS_POOL_FLAG_NONE);
    as_pool_add_extra_data_location_full(
        pool.get(), source.directory.c_str(), AS_FORMAT_STYLE_CATALOG,
        source.installation == InstallationKind::User ? AS_COMPONENT_SCOPE_USER : AS_COMPONENT_SCOPE_SYSTEM,
        source.remote.c_str()
    );
    GError* error = nullptr;
    if (!as_pool_load(pool.get(), nullptr, &error)) {
      std::string message = std::format("{}: {}", source.directory.string(), error ? error->message : "cannot be read");
      g_clear_error(&error);
      return std::unexpected(std::move(message));
    }
    std::unique_ptr<AsComponentBox, BoxDeleter> box(as_pool_get_components(pool.get()));
    for (guint i = 0; i < as_component_box_len(box.get()); ++i) {
      if (auto entry = entryOf(as_component_box_index(box.get(), i), source)) {
        entries.push_back(std::move(*entry));
      }
    }
  }
  return Catalog(std::move(entries));
}
