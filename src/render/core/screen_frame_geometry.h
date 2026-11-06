#pragma once

#include "render/core/color.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

struct ScreenFrameSpan {
  float begin = 0, end = 0;
  bool operator==(const ScreenFrameSpan&) const = default;
};

// A bar contributes geometry and material to a display-owned contour. Tracks
// are separate from widget layout and can later describe detached/freeform pieces.
inline constexpr std::size_t kMaxScreenFrameBands = 32;
struct ScreenFrameBand {
  std::string name;
  int edge = 0;
  float outer = 0, depth = 50, radius = 12, join = 24;
  std::array<ScreenFrameSpan, 3> spans{};
  Color fill{}, border{};
  float borderWidth = 0;
  bool operator==(const ScreenFrameBand&) const = default;
};

// Canonical island coordinates run along the bar, then inward from its screen edge.
struct ScreenFrameContour {
  float width = 0, height = 0, rail = 6, radius = 20;
  float depth = 50, islandRadius = 12, join = 24;
  int edge = 0; // top, right, bottom, left
  std::array<ScreenFrameSpan, 3> islands{};
  std::vector<ScreenFrameBand> bands;
  bool operator==(const ScreenFrameContour&) const = default;
};

inline std::array<ScreenFrameSpan, 3>
joinedScreenFrameSpans(std::array<ScreenFrameSpan, 3> spans, float length, float cutoff, float depth) {
  if (cutoff > 0) {
    if (spans[0].end > spans[0].begin && spans[0].begin <= cutoff)
      spans[0].begin = -depth;
    if (spans[2].end > spans[2].begin && length - spans[2].end <= cutoff)
      spans[2].end = length + depth;
  }
  return spans;
}

inline float screenFrameRoundedDistance(float x, float y, float halfWidth, float halfHeight, float radius) {
  radius = std::min(radius, std::min(halfWidth, halfHeight));
  const float qx = std::abs(x) - halfWidth + radius;
  const float qy = std::abs(y) - halfHeight + radius;
  return std::hypot(std::max(qx, 0.0F), std::max(qy, 0.0F)) + std::min(std::max(qx, qy), 0.0F) - radius;
}

// CPU counterpart of the frame shader, used for blur regions and geometry checks.
inline float screenFrameDistance(const ScreenFrameContour& g, float x, float y) {
  float d = -screenFrameRoundedDistance(
      x - g.width / 2, y - g.height / 2, g.width / 2 - g.rail, g.height / 2 - g.rail, g.radius
  );
  const bool vertical = g.edge == 1 || g.edge == 3;
  const float cross = vertical ? g.width : g.height;
  const float u = vertical ? y : x;
  const float v = g.edge == 1 ? g.width - x : g.edge == 2 ? g.height - y : g.edge == 3 ? x : y;
  for (const auto span : g.islands) {
    if (span.end <= span.begin)
      continue;
    const float island = screenFrameRoundedDistance(
        u - (span.begin + span.end) / 2, v - (g.depth - cross) / 2, (span.end - span.begin) / 2, (g.depth + cross) / 2,
        g.islandRadius
    );
    const float h = g.join > 0 ? std::max(g.join - std::abs(d - island), 0.0F) / g.join : 0.0F;
    d = std::min(d, island) - h * h * g.join * 0.25F;
  }
  for (const auto& band : g.bands) {
    const bool side = band.edge == 1 || band.edge == 3;
    const float bandU = side ? y : x;
    const float bandV = band.edge == 1 ? g.width - x : band.edge == 2 ? g.height - y : band.edge == 3 ? x : y;
    const float outer = band.outer > 0 ? band.outer : -(side ? g.width : g.height);
    for (const auto span : band.spans) {
      if (span.end <= span.begin)
        continue;
      const float shape = screenFrameRoundedDistance(
          bandU - (span.begin + span.end) / 2, bandV - (band.depth + outer) / 2, (span.end - span.begin) / 2,
          (band.depth - outer) / 2, band.radius
      );
      const float h = band.join > 0 ? std::max(band.join - std::abs(d - shape), 0.0F) / band.join : 0.0F;
      d = std::min(d, shape) - h * h * band.join * 0.25F;
    }
  }
  return d;
}

struct ScreenFrameStyle {
  ScreenFrameContour contour;
  Color fill{}, border{};
  float borderWidth = 0, offsetX = 0, offsetY = 0;
  std::array<float, 4> exclusion{};
  bool operator==(const ScreenFrameStyle&) const = default;
};
