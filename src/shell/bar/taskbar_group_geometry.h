#pragma once

#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace noctalia::bar {

  // Fit the entire group before measuring its individual pieces. Growing decorative insets must
  // never steal space from an icon that was larger at a lower requested scale.
  [[nodiscard]] inline float fitTaskbarGroupScale(
      float requested, float shellCross, float iconScale, bool outerCapsule, bool groupCapsule, float outerBorderWidth
  ) {
    if (shellCross <= 0.0F)
      return requested;
    const float unitTile = Style::baseGlyphSize * iconScale + 2.0F * Style::spaceXs * 0.35F;
    // Group layout already compresses its cross-axis padding when space is tight.
    // Reserve its minimum clearance here, rather than the full decorative padding.
    const float unitGroup = unitTile + 2.0F * Style::spaceXs * 0.35F
        + (groupCapsule ? 2.0F * Style::borderWidth : 0.0F);
    const float unitNesting =
        outerCapsule && groupCapsule ? 2.0F * (outerBorderWidth + Style::borderWidth + Style::spaceXs * 0.25F) : 0.0F;
    // Leave room for integer-pixel rounding at the fitted boundary.
    return std::min(requested, std::max(0.1F, (shellCross - 2.0F) / (unitGroup + unitNesting)));
  }

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
