#pragma once

#include "config/config_types.h"
#include "render/core/screen_frame_geometry.h"
#include "shell/bar/screen_wrap_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace screen_wrap {
  // The unused intersection of the top and left tracks owns the shell mark.
  // It is decoration on the shared frame, not a configured bar widget.
  inline Rect topLeftBrandingRect(std::span<const BarConfig> bars) {
    int top = 0, left = 0;
    int topThickness = 0, leftThickness = 0;
    for (const auto& bar : bars) {
      if (!bar.enabled || !bar.screenWrap) continue;
      const int depth = bar.marginEdge + bar.thickness;
      if (bar.position == "top") {
        top = std::max(top, depth);
        topThickness = std::max(topThickness, bar.thickness);
      }
      if (bar.position == "left") {
        left = std::max(left, depth);
        leftThickness = std::max(leftThickness, bar.thickness);
      }
    }
    // Oversize the artwork, keeping its box inside the screen edges.
    // Preserve a 60 px mark at a 40 px narrower joining bar.
    // Edge margins position the mark but do not change the artwork's scale.
    constexpr float ratio = 60.0F / 40.0F;
    const int size = static_cast<int>(std::lround(static_cast<float>(std::min(topThickness, leftThickness)) * ratio));
    return size > 0 ? Rect{std::max(0, (left - size) / 2), std::max(0, (top - size) / 2), size, size} : Rect{};
  }

  // Both visible bar shapes must actually reach the corner; the frame rails
  // alone do not constitute the top/left bar join.
  inline bool hasTopLeftJoin(const ScreenFrameContour& contour) {
    bool top = false, left = false;
    for (const auto& band : contour.bands) {
      if (band.edge != 0 && band.edge != 3) continue;
      const bool reachesCorner = std::ranges::any_of(band.spans, [](const ScreenFrameSpan& span) {
        return span.begin <= 0.0F && span.end > 0.0F;
      });
      if (band.edge == 0) top |= reachesCorner;
      if (band.edge == 3) left |= reachesCorner;
    }
    return top && left;
  }

  inline int edgeIndex(std::string_view edge) {
    return edge == "right" ? 1 : edge == "bottom" ? 2 : edge == "left" ? 3 : 0;
  }

  // Stable configured tracks, independent of widget sizes and island animation.
  struct Tracks {
    std::array<int, 4> depth{};
    int offset = 0;
    int startInset = 0, endInset = 0;
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
    const auto inset = [&](int adjoiningDepth) {
      return adjoiningDepth > 0 ? adjoiningDepth + bar.wrapJoinedCornerGap : bar.wrapWidth + bar.wrapFreeCornerGap;
    };
    result.startInset = inset(startDepth);
    result.endInset = inset(endDepth);
    return result;
  }

  // One reservation per physical edge: combine requests by maximum, never sum
  // animated silhouettes or multiple bars' requests for the same work area.
  inline std::array<int, 4> reservations(std::span<const BarConfig> bars, std::span<const int> requests) {
    std::array<int, 4> result{};
    int clearance = 0, rail = 0;
    bool reserving = false;
    for (std::size_t i = 0; i < bars.size(); ++i) {
      const auto& b = bars[i];
      rail = std::max(rail, b.wrapWidth);
      if (requests[i] <= 0)
        continue;
      reserving = true;
      clearance = std::max(clearance, b.wrapClearance);
      result[edgeIndex(b.position)] = std::max(result[edgeIndex(b.position)], requests[i]);
    }
    for (auto& edge : result) {
      if (reserving)
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

  // Opaque wallpaper covers only stable bar tracks, never an expanded panel or
  // animated island footprint. Split corners once to avoid overlapping alpha.
  inline std::array<Rect, 4> wallpaperBackingRects(int width, int height, std::array<int, 4> depths) {
    for (auto& depth : depths) depth = std::max(0, depth);
    depths = boundedReservations(depths, width, height);
    const auto [top, right, bottom, left] = depths;
    const int middle = std::max(0, height - top - bottom);
    return {{{0, 0, width, top}, {width - right, top, right, middle},
             {0, height - bottom, width, bottom}, {0, top, left, middle}}};
  }
} // namespace screen_wrap
