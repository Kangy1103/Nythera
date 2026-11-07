#include "i18n/i18n_service.h"
#include "cli/schema_msg.h"
#include <nlohmann/json.hpp>
#include <fstream>

#include <cstdio>
#include <filesystem>
#include <print>
#include <set>
#include <string>

namespace {

  bool fail(const char* message, const std::string& value) {
    std::println(stderr, "i18n_supported_languages_test: {}: {}", message, value);
    return false;
  }

} // namespace

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::println(stderr, "i18n_supported_languages_test: expected translations directory");
    return 1;
  }

  const std::filesystem::path translationsDir(argv[1]);

  std::set<std::string> supported;
  for (const auto& language : i18n::kSupportedLanguages) {
    supported.insert(std::string(language.code));
  }

  bool ok = true;
  // Action-picker labels are built dynamically, so literal-key scanners cannot
  // detect when a newly registered command lacks its English fallback text.
  std::ifstream englishFile(translationsDir / "en.json");
  const auto english = nlohmann::json::parse(englishFile);
  const auto& actions = english.at("settings").at("widgets").at("actions").at("commands");
  for (const auto& command : nythera::cli::kMsgCmd.subcommands) {
    const std::string name(command.name);
    for (const auto* field : {"label", "description"}) {
      if (!actions.contains(name) || !actions.at(name).contains(field)
          || !actions.at(name).at(field).is_string() || actions.at(name).at(field).get<std::string>().empty())
        ok = fail("action command is missing translation", name + "." + field) && ok;
    }
  }
  for (const auto& language : i18n::kSupportedLanguages) {
    const std::filesystem::path path = translationsDir / (std::string(language.code) + ".json");
    if (!std::filesystem::is_regular_file(path)) {
      ok = fail("supported language has no catalog", std::string(language.code)) && ok;
    }
  }

  for (const auto& entry : std::filesystem::directory_iterator(translationsDir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }
    const std::string code = entry.path().stem().string();
    if (!supported.contains(code)) {
      ok = fail("catalog is missing from kSupportedLanguages", code) && ok;
    }
  }

  return ok ? 0 : 1;
}
