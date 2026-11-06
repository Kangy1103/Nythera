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

  // Hide only the shared-edge stroke beyond the surface's clip; the other
  // three sides remain outlined. One extra pixel keeps antialiasing off the join.
  inline RectInsets outlineExtension(std::string_view edge, float stroke) {
    const float extent = stroke > 0 ? stroke + 1.0F : 0;
    if (edge == "bottom")
      return {0, 0, 0, extent};
    if (edge == "left")
      return {extent, 0, 0, 0};
    if (edge == "right")
      return {0, 0, extent, 0};
    return {0, extent, 0, 0};
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
