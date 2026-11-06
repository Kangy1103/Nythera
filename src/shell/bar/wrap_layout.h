#pragma once

#include "config/config_types.h"

#include <algorithm>
#include <array>
#include <span>

namespace screen_wrap {
  inline int edgeIndex(std::string_view edge) {
    return edge == "right" ? 1 : edge == "bottom" ? 2 : edge == "left" ? 3 : 0;
  }

  // Stable configured tracks, independent of widget sizes and island animation.
  struct Tracks {
    std::array<int, 4> depth{};
    int offset = 0;
    int endInset = 0;
  };
  inline Tracks tracks(std::span<const BarConfig> bars, std::size_t selected) {
    Tracks result;
    const auto& bar = bars[selected];
    const int edge = edgeIndex(bar.position);
    for (std::size_t i = 0; i < bars.size(); ++i) {
      const auto& b = bars[i];
      if (!b.enabled || !b.screenWrap)
        continue;
      const int e = edgeIndex(b.position);
      if (i < selected && e == edge)
        result.offset += b.thickness;
      result.depth[e] += b.thickness;
    }
    const bool vertical = edge == 1 || edge == 3;
    const int startDepth = result.depth[vertical ? 0 : 3];
    const int endDepth = result.depth[vertical ? 2 : 1];
    result.endInset = std::max(
        {bar.marginEnds, bar.wrapWidth + (bar.surfaceMode == "full" ? 0 : bar.wrapRadius),
         startDepth > 0 ? startDepth + bar.radius : 0, endDepth > 0 ? endDepth + bar.radius : 0}
    );
    return result;
  }

  // One reservation per physical edge: combine requests by maximum, never sum
  // animated silhouettes or multiple bars' requests for the same work area.
  inline std::array<int, 4> reservations(std::span<const BarConfig> bars, std::span<const int> requests) {
    std::array<int, 4> result{};
    int clearance = 0, rail = 0;
    for (std::size_t i = 0; i < bars.size(); ++i) {
      const auto& b = bars[i];
      rail = std::max(rail, b.wrapWidth);
      if (requests[i] <= 0)
        continue;
      clearance = std::max(clearance, b.wrapClearance);
      result[edgeIndex(b.position)] = std::max(result[edgeIndex(b.position)], requests[i]);
    }
    for (auto& edge : result) {
      if (clearance > 0)
        edge = std::max(edge, rail) + clearance;
    }
    return result;
  }
  inline std::array<int, 4> boundedReservations(std::array<int, 4> zones, int width, int height) {
    for (const auto axis : {std::array<int, 3>{3, 1, width}, std::array<int, 3>{0, 2, height}}) {
      const int limit = std::max(0, axis[2] - 1);
      const int sum = zones[axis[0]] + zones[axis[1]];
      if (sum > limit) {
        zones[axis[0]] = static_cast<int>(static_cast<long long>(zones[axis[0]]) * limit / sum);
        zones[axis[1]] = limit - zones[axis[0]];
      }
    }
    return zones;
  }
} // namespace screen_wrap
