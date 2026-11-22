#include "catalog/search.h"

#include <algorithm>
#include <glib.h>
#include <unordered_map>

namespace {
  std::vector<std::string> words(std::string_view folded) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < folded.size()) {
      const auto end = folded.find_first_of(" \t\n", start);
      if (end != start) {
        out.emplace_back(folded.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
      }
      if (end == std::string_view::npos) {
        break;
      }
      start = end + 1;
    }
    return out;
  }

  bool startsWord(std::string_view text, std::size_t pos) {
    if (pos == 0) {
      return true;
    }
    const char before = text[pos - 1];
    return before == ' ' || before == '-' || before == '.' || before == '_' || before == '(';
  }

  // How well one word of the query matches: 0 for not at all.
  int wordScore(std::string_view word, const etal::AppEntry& entry, std::string_view name, std::string_view summary) {
    int best = 0;
    if (name == word) {
      best = 100;
    } else if (name.starts_with(word)) {
      best = 60;
    } else if (const auto pos = name.find(word); pos != std::string_view::npos) {
      best = startsWord(name, pos) ? 45 : 25;
    }
    if (best >= 45) {
      return best;
    }
    for (const auto& keyword : entry.keywords) {
      const std::string folded = etal::fold(keyword);
      if (folded == word) {
        best = std::max(best, 35);
      } else if (folded.starts_with(word)) {
        best = std::max(best, 20);
      }
    }
    if (best == 0 && etal::fold(entry.id).find(word) != std::string::npos) {
      best = 15;
    }
    if (best == 0 && summary.find(word) != std::string_view::npos) {
      best = 10;
    }
    if (best == 0 && etal::fold(entry.developer).find(word) != std::string::npos) {
      best = 8;
    }
    return best;
  }

  bool passes(const etal::AppEntry& entry, const etal::SearchFilter& filter) {
    if (filter.onlyVerified && !entry.verified) {
      return false;
    }
    if (filter.onlyFree && !entry.freeLicense) {
      return false;
    }
    return filter.category.empty() || std::ranges::find(entry.categories, filter.category) != entry.categories.end();
  }
} // namespace

std::string etal::fold(std::string_view text) {
  char* folded = g_utf8_strdown(text.data(), static_cast<gssize>(text.size()));
  std::string out(folded ? folded : "");
  g_free(folded);
  return out;
}

std::vector<std::size_t> etal::search(const Catalog& catalog, std::string_view query, const SearchFilter& filter) {
  const auto& entries = catalog.entries();
  // One entry per app: the user installation's where both have it, else the first read.
  std::unordered_map<std::string_view, std::size_t> chosen;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    if (!passes(entry, filter)) {
      continue;
    }
    auto [it, inserted] = chosen.try_emplace(entry.id, i);
    if (!inserted && entries[it->second].installation != InstallationKind::User
        && entry.installation == InstallationKind::User) {
      it->second = i;
    }
  }

  const auto query_words = words(fold(query));
  struct Hit {
    std::size_t index;
    int score;
    std::string name;
  };
  std::vector<Hit> hits;
  hits.reserve(chosen.size());
  for (const auto& [id, index] : chosen) {
    const auto& entry = entries[index];
    std::string name = fold(entry.name);
    int score = 0;
    if (!query_words.empty()) {
      const std::string summary = fold(entry.summary);
      for (const auto& word : query_words) {
        const int s = wordScore(word, entry, name, summary);
        if (s == 0) {
          score = 0;
          break;
        }
        score += s;
      }
      if (score == 0) {
        continue;
      }
      if (entry.verified) {
        score += 3;
      }
    }
    hits.push_back({index, score, std::move(name)});
  }
  std::ranges::sort(hits, [](const Hit& a, const Hit& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    return a.name < b.name;
  });
  std::vector<std::size_t> out;
  out.reserve(hits.size());
  for (const auto& hit : hits) {
    out.push_back(hit.index);
  }
  return out;
}
