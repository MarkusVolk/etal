#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class HttpClient;

namespace etal {
  // Screenshots and icons from the web, kept in $XDG_CACHE_HOME/etal/images under a name derived
  // from the URL. Files older than a month are fetched again on their next use.
  class ImageCache {
  public:
    using Ready = std::function<void(const std::filesystem::path& file)>;

    explicit ImageCache(HttpClient& http);

    // Calls `ready` with the local file, on a later main-loop iteration or at once when it is
    // cached; with an empty path when the download failed. Requests for the same URL share one
    // download.
    void fetch(const std::string& url, Ready ready);
    // The cached file of `url`, whether or not it exists yet.
    std::filesystem::path fileFor(const std::string& url) const;

  private:
    HttpClient& m_http;
    std::filesystem::path m_directory;
    std::unordered_map<std::string, std::vector<Ready>> m_pending;
  };
} // namespace etal
