#include "net/image_cache.h"

#include "fs/xdg_dirs.h"
#include "net/http_client.h"

#include <chrono>
#include <glib.h>

namespace {
  constexpr auto kMaxAge = std::chrono::hours(24 * 30);

  std::string extensionOf(const std::string& url) {
    const auto slash = url.find_last_of('/');
    const auto dot = url.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
      return {};
    }
    std::string ext = url.substr(dot);
    if (const auto query = ext.find_first_of("?#"); query != std::string::npos) {
      ext.resize(query);
    }
    return ext.size() <= 5 ? ext : std::string();
  }
} // namespace

etal::ImageCache::ImageCache(HttpClient& http) : m_http(http) {
  try {
    m_directory = cacheHome() / "etal" / "images";
  } catch (const std::exception&) {
    m_directory = std::filesystem::temp_directory_path() / "etal-images";
  }
}

std::filesystem::path etal::ImageCache::fileFor(const std::string& url) const {
  char* hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, url.c_str(), static_cast<gssize>(url.size()));
  std::string name(hash);
  g_free(hash);
  name.resize(32);
  return m_directory / (name + extensionOf(url));
}

void etal::ImageCache::fetch(const std::string& url, Ready ready) {
  if (url.empty()) {
    ready({});
    return;
  }
  const auto file = fileFor(url);
  std::error_code error;
  const auto modified = std::filesystem::last_write_time(file, error);
  if (!error && std::filesystem::file_time_type::clock::now() - modified < kMaxAge) {
    ready(file);
    return;
  }
  auto [it, first] = m_pending.try_emplace(url);
  it->second.push_back(std::move(ready));
  if (!first) {
    return;
  }
  std::filesystem::create_directories(m_directory, error);
  m_http.download(url, file, [this, url, file](bool success) {
    auto node = m_pending.extract(url);
    if (node.empty()) {
      return;
    }
    for (auto& callback : node.mapped()) {
      callback(success ? file : std::filesystem::path());
    }
  });
}
