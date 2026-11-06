#include "config/legacy_profile_migration.h"

#include <cassert>
#include <fstream>
#include <unistd.h>

int main() {
  namespace fs = std::filesystem;
  using namespace nythera::config::legacy_profile;
  const auto root = fs::temp_directory_path() / ("nythera-migration-test-" + std::to_string(getpid()));
  fs::create_directories(root / "legacy/nested");
  fs::create_directories(root / "new");
  std::ofstream(root / "legacy/config.toml") << "command = 'noctalia msg status'\n";
  std::ofstream(root / "legacy/nested/data") << "private state";
  std::ofstream(root / "legacy/keep") << "old";
  std::ofstream(root / "new/keep") << "new";
  fs::create_directory_symlink(root / "legacy/nested", root / "legacy/link");
  auto log = [](const std::string&) {};
  auto result = migrate({{root / "legacy", root / "new"}, {root / "missing", root / "absent"}}, log);
  assert(result.failed == 0 && result.copied == 2 && result.preserved == 2);
  assert(fs::exists(root / "new/nested/data") && !fs::exists(root / "new/link"));
  std::ifstream in(root / "new/config.toml");
  std::string value((std::istreambuf_iterator<char>(in)), {});
  assert(value.find("nythera msg") != std::string::npos);
  std::ifstream old(root / "legacy/config.toml");
  std::string original((std::istreambuf_iterator<char>(old)), {});
  assert(original.find("noctalia msg") != std::string::npos);
  result = migrate({{root / "legacy", root / "new"}}, log);
  assert(result.copied == 0 && result.failed == 0);
  assert(migrate({{root / "legacy", root / "legacy/sub"}}, log).failed == 1);
  fs::remove_all(root / "new/nested");
  fs::create_directory_symlink(root / "legacy/nested", root / "new/nested");
  assert(migrate({{root / "legacy", root / "new"}}, log).failed > 0);
  fs::remove_all(root);
}
