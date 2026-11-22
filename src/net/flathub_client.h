#pragma once

#include "net/flathub_parse.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class HttpClient;

namespace etal {
  // What only Flathub's web API knows: which apps trend, the app of the day, how often an app is
  // installed. Answers are kept in $XDG_CACHE_HOME/etal/api; a cached one is used while it is
  // fresh, and again, however old, when the network does not answer.
  class FlathubClient {
  public:
    using Ids = std::function<void(std::vector<std::string>)>;

    explicit FlathubClient(HttpClient& http);

    // name: "trending", "popular", "recently-added" or "recently-updated". The callback may run
    // before this returns, when the answer is cached.
    void collection(const std::string& name, int count, Ids done);
    void appOfTheDay(std::function<void(std::string)> done);
    void stats(const std::string& appId, std::function<void(std::optional<FlathubStats>)> done);

  private:
    void get(const std::string& path, const std::string& cacheName, std::chrono::seconds maxAge, std::function<void(std::string)> done);

    HttpClient& m_http;
    std::filesystem::path m_directory;
  };
} // namespace etal
