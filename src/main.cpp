#include "app/application.h"
#include "core/application_identity.h"

#include <string>
#include <vector>

int main(int argc, char* argv[]) {
  setApplicationIdentity({.name = "etal", .envPrefix = "ETAL"});
  std::vector<std::string> files;
  for (int index = 1; index < argc; ++index) {
    files.emplace_back(argv[index]);
  }
  return etal::runApplication(files);
}
