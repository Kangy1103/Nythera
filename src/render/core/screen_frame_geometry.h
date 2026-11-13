#pragma once

#include "render/core/color.h"
#include "render/core/snapshot_backdrop.h"
#include "render/core/surface_haze.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
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
  bool edgeAttached = true;
  float outer = 0, depth = 50, radius = 12, join = 24;
  bool softChamfer = false;
  float chamferUpper = 12, chamferLower = 36;
  std::array<ScreenFrameSpan, 3> spans{};
  ScreenFrameSpan neckSpan{};
  float neckDepth = 0;
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

// Replace the owning island with its deeper tool body. Blending both rounded
// rectangles would bow the shared sides around the old bar-depth corners.
inline void appendScreenFrameTool(ScreenFrameContour& contour, ScreenFrameBand& bar,
                                  ScreenFrameSpan span, float depth, int islandIndex) {
  auto tool = bar;
  tool.name += ":panel";
  tool.softChamfer = false;
  tool.spans = {};
  tool.spans[1] = span;
  tool.depth = depth;
  if (islandIndex >= 0 && islandIndex < static_cast<int>(bar.spans.size())) {
    const auto island = bar.spans[islandIndex];
    if (span.begin > island.begin || span.end < island.end) {
      tool.neckSpan = island;
      tool.neckDepth = bar.depth;
    }
    bar.spans[islandIndex] = {};
  }
  contour.bands.push_back(std::move(tool));
}

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

struct ScreenFramePoint {
  float x = 0, y = 0;
};

inline float screenFrameRoundedQuadDistance(
    const std::array<ScreenFramePoint, 4>& vertices, float x, float y, float radius
) {
  std::array<ScreenFramePoint, 4> inset{};
  for (int i = 0; i < 4; ++i) {
    const int prev = i == 0 ? 3 : i - 1;
    const int next = i == 3 ? 0 : i + 1;
    const auto p = vertices[i], a = vertices[prev], b = vertices[next];
    const float e1x = p.x - a.x, e1y = p.y - a.y;
    const float e2x = b.x - p.x, e2y = b.y - p.y;
    const float l1 = std::hypot(e1x, e1y), l2 = std::hypot(e2x, e2y);
    if (l1 <= 0.0F || l2 <= 0.0F)
      return std::numeric_limits<float>::infinity();
    const ScreenFramePoint n1{-e1y / l1, e1x / l1};
    const ScreenFramePoint n2{-e2y / l2, e2x / l2};
    const float denominator = std::max(1.0F + n1.x * n2.x + n1.y * n2.y, 1e-4F);
    const float scale = radius / denominator;
    inset[i] = {p.x + (n1.x + n2.x) * scale, p.y + (n1.y + n2.y) * scale};
  }

  bool inside = true;
  float nearestSquared = std::numeric_limits<float>::infinity();
  for (int i = 0; i < 4; ++i) {
    const int next = i == 3 ? 0 : i + 1;
    const auto a = inset[i], b = inset[next];
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float px = x - a.x, py = y - a.y;
    inside = inside && dx * py - dy * px >= 0.0F;
    const float lengthSquared = dx * dx + dy * dy;
    const float t = lengthSquared > 0.0F
        ? std::clamp((px * dx + py * dy) / lengthSquared, 0.0F, 1.0F)
        : 0.0F;
    const float qx = px - t * dx, qy = py - t * dy;
    nearestSquared = std::min(nearestSquared, qx * qx + qy * qy);
  }
  const float distance = std::sqrt(nearestSquared);
  return (inside ? -distance : distance) - radius;
}

inline float screenFrameChamferedBandDistance(
    float u, float v, ScreenFrameSpan span, float depth, float rail, bool chamferStart, bool chamferEnd,
    float radius = 36.0F
) {
  // Keep the upper polygon corners outside the display: the frame's join
  // smoothing then rounds the meeting with the rail without a second curve.
  const float outer = -2.0F * depth;
  const float extent = depth - outer;
  const float leftTop = span.begin - (chamferStart ? extent : 0.0F);
  const float rightTop = span.end + (chamferEnd ? extent : 0.0F);
  const std::array<ScreenFramePoint, 4> vertices{{
      {leftTop, outer}, {rightTop, outer}, {span.end, depth}, {span.begin, depth}
  }};
  const float safeRadius = std::min({radius, (depth - rail) * 0.7F,
                                     (span.end - span.begin) * 0.45F});
  return screenFrameRoundedQuadDistance(vertices, u, v, safeRadius);
}

// CPU counterpart of the frame shader, used for blur regions and geometry checks.
inline float screenFrameDistance(const ScreenFrameContour& g, float x, float y) {
  float d = g.rail < 0 ? 1e6F : -screenFrameRoundedDistance(
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
    const float outer = band.edgeAttached ? -(side ? g.width : g.height) : band.outer;
    for (const auto span : band.spans) {
      if (span.end <= span.begin)
        continue;
      float shape = screenFrameRoundedDistance(
          bandU - (span.begin + span.end) / 2, bandV - (band.depth + outer) / 2, (span.end - span.begin) / 2,
          (band.depth - outer) / 2, band.radius
      );
      const float chamferGap = std::max(0.0F, band.depth - g.rail);
      const float edgeLength = side ? g.height : g.width;
      const bool chamferStart = band.softChamfer && band.edgeAttached && chamferGap > 0.0F
          && span.begin > chamferGap && span.begin < edgeLength;
      const bool chamferEnd = band.softChamfer && band.edgeAttached && chamferGap > 0.0F
          && span.end > 0.0F && span.end + chamferGap < edgeLength;
      const bool chamfered = chamferStart || chamferEnd;
      if (chamfered)
        shape = screenFrameChamferedBandDistance(bandU, bandV, span, band.depth, g.rail,
                                                 chamferStart, chamferEnd, band.chamferLower);
      if (band.neckSpan.end > band.neckSpan.begin) {
        const auto neck = band.neckSpan;
        const float nd = screenFrameRoundedDistance(
            bandU - (neck.begin + neck.end) / 2, bandV - (band.neckDepth + outer) / 2,
            (neck.end - neck.begin) / 2, (band.neckDepth - outer) / 2, band.radius);
        const float h = band.join > 0 ? std::max(band.join - std::abs(shape - nd), 0.0F) / band.join : 0;
        shape = std::min(shape, nd) - h * h * band.join * 0.25F;
        // Round the inward step, but never inflate coincident outer sides.
        const float begin = std::min(span.begin, neck.begin), end = std::max(span.end, neck.end);
        const float depth = std::max(band.depth, band.neckDepth);
        shape = std::max(shape, screenFrameRoundedDistance(
            bandU - (begin + end) / 2, bandV - (depth + outer) / 2,
            (end - begin) / 2, (depth - outer) / 2, 0));
      }
      const float join = chamfered
          ? std::min(std::min(band.join, 12.0F) * band.chamferUpper / 12.0F, chamferGap)
          : band.join;
      const float h = join > 0
          ? std::max(join - std::abs(d - shape), 0.0F) / join
          : 0.0F;
      d = std::min(d, shape) - h * h * join * 0.25F;
    }
  }
  return d;
}

inline ScreenFrameSpan joinedPanelFrameSpan(ScreenFrameSpan span, float length, float cutoff, float depth) {
  if (cutoff > 0) {
    if (span.begin <= cutoff) span.begin = -depth;
    if (length - span.end <= cutoff) span.end = length + depth;
  }
  return span;
}

struct ScreenFrameStyle {
  ScreenFrameContour contour;
  Color fill{}, border{};
  float borderWidth = 0, offsetX = 0, offsetY = 0;
  std::array<float, 4> exclusion{};
  ScreenFrameBackdrop backdrop;
  bool surfaceBackground = false;
  SurfaceHazeStyle haze;
  std::string hazeProfile;
  std::array<float, 4> hazeBounds{};
  bool operator==(const ScreenFrameStyle&) const = default;
};
