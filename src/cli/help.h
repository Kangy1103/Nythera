#pragma once

#include "cli/schema.h"

#include <string>
#include <string_view>

namespace nythera::cli {

  [[nodiscard]] std::string renderHelp(const Command& command, std::string_view path);
  [[nodiscard]] std::string renderArgsSpec(const Command& command);

} // namespace nythera::cli
