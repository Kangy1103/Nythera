#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace screen_wrap {
  struct Rect {
    int x = 0, y = 0, width = 0, height = 0;
  };

  struct Geometry {
    int width, height, left, top, right, bottom, radius;
    // Disjoint buffers: the long strips own the corners. No full-screen buffer.
    std::array<Rect, 4> strips;
  };

  inline Geometry geometry(
      int width, int height, int rail, int radius, int thickness, std::string_view edge, bool full, int outline = 0
  ) {
    width = std::max(1, width);
    height = std::max(1, height);
    rail = std::clamp(rail, 1, std::max(1, std::min(width, height) / 4));
    Geometry g{width, height, rail, rail, rail, rail, 0, {}};
    if (full) {
      const bool vertical = edge == "left" || edge == "right";
      const int depth = std::clamp(thickness, rail, std::max(rail, (vertical ? width : height) - rail - 2));
      if (edge == "left")
        g.left = depth;
      else if (edge == "right")
        g.right = depth;
      else if (edge == "bottom")
        g.bottom = depth;
      else
        g.top = depth;
    }
    g.radius =
        std::clamp(radius, 0, std::max(0, std::min(width - g.left - g.right, height - g.top - g.bottom) / 2 - 1));
    // An inside outline needs buffer space even with square (zero-radius) corners.
    const int padding = std::clamp(
        std::max(g.radius, outline), 0,
        std::max(0, std::min(width - g.left - g.right, height - g.top - g.bottom) / 2 - 1)
    );
    const int topEnd = g.top + padding;
    const int bottomStart = height - g.bottom - padding;
    g.strips = {
        {{0, 0, width, topEnd},
         {0, bottomStart, width, height - bottomStart},
         {0, topEnd, g.left + padding, bottomStart - topEnd},
         {width - g.right - padding, topEnd, g.right + padding, bottomStart - topEnd}}
    };
    return g;
  }
} // namespace screen_wrap
