#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace nythera::plugins {
  // Upstream plugin repositories still publish the `noctalia` Luau global.
  // Translate that external source API at ingestion, not via a runtime alias.
  // Catalogue URLs, comments and long strings stay intact. Host CLI/helper
  // literals are rewired explicitly alongside the API global.
  inline std::string importUpstreamSource(std::string_view source) {
    std::string result;
    std::size_t i = 0;
    const auto longEnd = [&](std::size_t start) -> std::size_t {
      if (source[start] != '[')
        return start;
      auto j = start + 1;
      while (j < source.size() && source[j] == '=')
        ++j;
      if (j >= source.size() || source[j] != '[')
        return start;
      const auto close = "]" + std::string(j - start - 1, '=') + "]";
      const auto end = source.find(close, j + 1);
      return end == std::string_view::npos ? source.size() : end + close.size();
    };
    while (i < source.size()) {
      const auto start = i;
      if (source[i] == '\'' || source[i] == '"' || source[i] == '`') {
        const char quote = source[i++];
        while (i < source.size()) {
          if (source[i] == '\\') {
            i = std::min(i + 2, source.size());
            continue;
          }
          if (source[i++] == quote)
            break;
        }
      } else if (source.substr(i, 2) == "--") {
        i += 2;
        const auto end = i < source.size() ? longEnd(i) : i;
        if (end > i)
          i = end;
        else {
          const auto nl = source.find('\n', i);
          i = nl == source.npos ? source.size() : nl;
        }
      } else if (const auto end = longEnd(i); end > i) {
        i = end;
      } else if (std::isalpha(static_cast<unsigned char>(source[i])) || source[i] == '_') {
        while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_'))
          ++i;
        const auto token = source.substr(start, i - start);
        result += token == "noctalia" ? "nythera" : token;
        continue;
      } else {
        ++i;
      }
      auto segment = std::string(source.substr(start, i - start));
      if ((source[start] == '\'' || source[start] == '"') && segment.size() >= 2) {
        auto value = segment.substr(1, segment.size() - 2);
        // External plugins also invoke the host executable and helper APIs.
        // Rewire those host contracts; leave catalogue IDs and URLs alone.
        if (value == "noctalia" || value.starts_with("noctalia "))
          value.replace(0, 8, "nythera");
        std::size_t pos = 0;
        while ((pos = value.find("dev.noctalia.", pos)) != std::string::npos) {
          value.replace(pos, 13, "dev.nythera.");
          pos += 12;
        }
        segment = std::string(1, segment.front()) + value + segment.back();
      }
      result += segment;
    }
    return result;
  }
} // namespace nythera::plugins
