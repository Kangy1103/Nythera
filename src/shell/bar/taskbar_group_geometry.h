#pragma once

#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace noctalia::bar {

  [[nodiscard]] inline std::string taskbarWorkspaceLabel(std::string_view text, std::size_t maxChars) {
    const bool numeric = !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
    return maxChars == 0 || numeric ? std::string(text) : StringUtils::truncateUtf8CodePoints(text, maxChars);
  }

  struct TaskbarBadgeSize {
    float width = 0.0F;
    float height = 0.0F;
  };

  [[nodiscard]] inline TaskbarBadgeSize
  taskbarBadgeSize(float textWidth, float textHeight, float minHeight, float textScale) {
    const float height = std::ceil(std::max(minHeight, textHeight + 2.0F * textScale));
    return {std::ceil(std::max(height, textWidth + 2.0F * Style::spaceXs * textScale)), height};
  }

  // The resolver takes physical pixels; Image takes logical pixels and applies output scale itself.
  [[nodiscard]] inline int taskbarIconRasterSize(float logicalSize, float renderScale = 1.0F) {
    return std::max(1, static_cast<int>(std::ceil(logicalSize * renderScale * 2.0F)));
  }

  // External horizontal badges protrude before the capsule. Reserve their actual remaining
  // extent plus a gap; the previous fractional reservation overlapped the first application.
  [[nodiscard]] inline float
  taskbarBadgeContentStart(bool vertical, bool corner, float badgeMain, float outlineInset, float gap) {
    return (vertical ? outlineInset + badgeMain : badgeMain * (corner ? 0.8F : 0.6F)) + gap;
  }
} // namespace noctalia::bar
