#include "net/flathub_client.h"

#include "fs/xdg_dirs.h"
#include "net/http_client.h"

#include <format>
#include <fstream>
#include <glib.h>
#include <sstream>

namespace {
  constexpr const char* kBase = "https://flathub.org/api/v2";

  std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
  }

  std::string today() {
    GDateTime* now = g_date_time_new_now_utc();
    char* text = g_date_time_format(now, "%F");
    std::string out(text);
    g_free(text);
    g_date_time_unref(now);
    return out;
  }
} // namespace

etal::FlathubClient::FlathubClient(HttpClient& http) : m_http(http) {
  try {
    m_directory = cacheHome() / "etal" / "api";
  } catch (const std::exception&) {
    m_directory = std::filesystem::temp_directory_path() / "etal-api";
  }
}

void etal::FlathubClient::get(
    const std::string& path, const std::string& cacheName, std::chrono::seconds maxAge, std::function<void(std::string)> done
) {
  const auto file = m_directory / cacheName;
  std::error_code error;
  const auto modified = std::filesystem::last_write_time(file, error);
  if (!error && std::filesystem::file_time_type::clock::now() - modified < maxAge) {
    done(readFile(file));
    return;
  }
  HttpRequest request;
  request.url = std::string(kBase) + path;
  request.followRedirects = true;
  m_http.request(std::move(request), [directory = m_directory, file, done = std::move(done)](HttpResponse response) {
    if (response.transportOk && response.status == 200) {
      std::error_code ignored;
      std::filesystem::create_directories(directory, ignored);
      g_file_set_contents(file.c_str(), response.body.data(), static_cast<gssize>(response.body.size()), nullptr);
      done(std::move(response.body));
    } else {
      // Offline or an error: the old answer is better than none.
      done(readFile(file));
    }
  });
}

void etal::FlathubClient::collection(const std::string& name, int count, Ids done) {
  get(
      std::format("/collection/{}?page=1&per_page={}", name, count), std::format("collection-{}.json", name), std::chrono::hours(6),
      [done = std::move(done)](std::string body) { done(parseCollection(body)); }
  );
}

void etal::FlathubClient::appOfTheDay(std::function<void(std::string)> done) {
  const std::string date = today();
  get(
      "/app-picks/app-of-the-day/" + date, std::format("app-of-the-day-{}.json", date), std::chrono::hours(24 * 7),
      [done = std::move(done)](std::string body) { done(parseAppOfTheDay(body)); }
  );
}

void etal::FlathubClient::stats(const std::string& appId, std::function<void(std::optional<FlathubStats>)> done) {
  if (appId.empty() || appId.find('/') != std::string::npos) {
    done(std::nullopt);
    return;
  }
  get(
      std::format("/stats/{}?all=false&days=30", appId), std::format("stats-{}.json", appId), std::chrono::hours(24),
      [done = std::move(done)](std::string body) { done(parseStats(body)); }
  );
}
