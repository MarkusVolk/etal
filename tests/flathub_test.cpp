#include "check.h"
#include "net/flathub_parse.h"

int main() {
  const auto ids = etal::parseCollection(R"({"hits":[{"app_id":"org.a.A","name":"A"},{"name":"no id"},{"app_id":"org.b.B"}],"totalHits":2})");
  CHECK(ids.size() == 2);
  CHECK(ids[0] == "org.a.A" && ids[1] == "org.b.B");
  CHECK(etal::parseCollection("").empty());
  CHECK(etal::parseCollection("[1,2]").empty());
  CHECK(etal::parseCollection(R"({"hits":"none"})").empty());
  CHECK(etal::parseCollection("<html>502</html>").empty());

  CHECK(etal::parseAppOfTheDay(R"({"app_id":"org.c.C","day":"2026-11-22"})") == "org.c.C");
  CHECK(etal::parseAppOfTheDay(R"({"detail":"Not Found"})").empty());
  CHECK(etal::parseAppOfTheDay("").empty());

  const auto stats = etal::parseStats(R"({"installs_total":12666780,"installs_last_month":214916,"installs_per_day":{}})");
  CHECK(stats.has_value());
  CHECK(stats->installsTotal == 12666780 && stats->installsLastMonth == 214916);
  CHECK(!etal::parseStats(R"({"detail":"Not Found"})").has_value());
  CHECK(!etal::parseStats("null").has_value());
  return 0;
}
