#pragma once

#include "render/core/render_styles.h"

#include <algorithm>
#include <string_view>

namespace dynamic_island_style {
  // Continue the neck past the surface's inner edge. Its end stroke and rounded
  // corners then lie outside the island buffer, instead of cutting across the tool.
  inline float joinExtension(float progress, float radius, float borderWidth) {
    return progress > 0.0F ? std::max(2.0F * radius, borderWidth + 2.0F) : 0.0F;
  }

  inline Radii panelRadii(std::string_view edge, float radius) {
    if (edge == "bottom")
      return {radius, radius, 0, 0};
    if (edge == "left")
      return {0, radius, radius, 0};
    if (edge == "right")
      return {radius, 0, 0, radius};
    return {0, 0, radius, radius};
  }
} // namespace dynamic_island_style
