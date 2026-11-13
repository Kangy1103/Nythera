#pragma once

#include "render/core/render_styles.h"
#include "render/core/screen_frame_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

enum class AttachedRevealDirection : std::uint8_t {
  Down,
  Up,
  Right,
  Left,
};

struct AttachedPanelGeometry {
  std::string hazeProfile;
  // Dynamic-island hosts follow this same reveal instead of starting a second animation.
  float revealProgress = 1.0F;
  int islandIndex = -1;
  std::array<float, 2> islandToolSpan{};
  std::array<float, 2> islandSpan{}; // animated output-local span for ordinary islands
  float islandExtent = 0.0F;
  float islandDepth = 0.0F;
  std::array<float, 4> islandSurface{}; // output-local backdrop ownership rectangle
  ScreenFrameBackdrop backdrop;
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
  // Full panel corner radius; used for the away-side convex corners, which are visible
  // throughout the open/close animation and should always render rounded.
  float cornerRadius = 0.0F;
  // Bar-side concave-corner radius. Animated with the reveal progress: zero while the
  // bg's bar-side edge is still hidden behind the bar, ramps to cornerRadius as the
  // bulges slide into view near the end of the open animation.
  float bulgeRadius = 0.0F;
};

namespace attached_panel {

  [[nodiscard]] inline float mainAxisRevealClipExtent(
      float surfaceExtent, float compactExtent, float panelExtent, float progress, bool keepPanelFullWidth
  ) noexcept {
    if (keepPanelFullWidth) {
      return surfaceExtent;
    }
    return std::lerp(compactExtent, panelExtent, std::clamp(progress, 0.0F, 1.0F));
  }

  struct RevealClipRect {
    float x = 0, y = 0, width = 0, height = 0;
  };

  // The panel keeps its full cross-axis extent. Its slide may reveal only the
  // body side of the bar: no control pixels may enter the island's neck.
  [[nodiscard]] inline RevealClipRect stagedPanelBodyClip(
      AttachedRevealDirection direction, float surfaceWidth, float surfaceHeight,
      float insetX, float insetY, float panelWidth, float panelHeight
  ) noexcept {
    switch (direction) {
    case AttachedRevealDirection::Down:
      return {0, insetY, surfaceWidth, std::max(0.0F, surfaceHeight - insetY)};
    case AttachedRevealDirection::Up:
      return {0, 0, surfaceWidth, std::clamp(insetY + panelHeight, 0.0F, surfaceHeight)};
    case AttachedRevealDirection::Right:
      return {insetX, 0, std::max(0.0F, surfaceWidth - insetX), surfaceHeight};
    case AttachedRevealDirection::Left:
      return {0, 0, std::clamp(insetX + panelWidth, 0.0F, surfaceWidth), surfaceHeight};
    }
    return {};
  }

  [[nodiscard]] inline CornerShapes cornerShapes(std::string_view barPosition) {
    if (barPosition == "bottom") {
      return CornerShapes{
          .tl = CornerShape::Convex,
          .tr = CornerShape::Convex,
          .br = CornerShape::Concave,
          .bl = CornerShape::Concave,
      };
    }
    if (barPosition == "left") {
      return CornerShapes{
          .tl = CornerShape::Concave,
          .tr = CornerShape::Convex,
          .br = CornerShape::Convex,
          .bl = CornerShape::Concave,
      };
    }
    if (barPosition == "right") {
      return CornerShapes{
          .tl = CornerShape::Convex,
          .tr = CornerShape::Concave,
          .br = CornerShape::Concave,
          .bl = CornerShape::Convex,
      };
    }
    // top (default)
    return CornerShapes{
        .tl = CornerShape::Concave,
        .tr = CornerShape::Concave,
        .br = CornerShape::Convex,
        .bl = CornerShape::Convex,
    };
  }

  [[nodiscard]] inline RectInsets logicalInset(std::string_view barPosition, float radius) {
    const bool vertical = (barPosition == "left" || barPosition == "right");
    if (vertical) {
      return RectInsets{
          .left = 0.0F,
          .top = radius,
          .right = 0.0F,
          .bottom = radius,
      };
    }
    return RectInsets{
        .left = radius,
        .top = 0.0F,
        .right = radius,
        .bottom = 0.0F,
    };
  }

  [[nodiscard]] inline AttachedRevealDirection revealDirection(std::string_view barPosition) {
    if (barPosition == "bottom") {
      return AttachedRevealDirection::Up;
    }
    if (barPosition == "left") {
      return AttachedRevealDirection::Right;
    }
    if (barPosition == "right") {
      return AttachedRevealDirection::Left;
    }
    return AttachedRevealDirection::Down;
  }

  [[nodiscard]] inline Radii cornerRadii(std::string_view barPosition, float radius) {
    if (barPosition == "bottom") {
      return Radii{0.0F, 0.0F, radius, radius};
    }
    if (barPosition == "left") {
      return Radii{0.0F, radius, radius, 0.0F};
    }
    if (barPosition == "right") {
      return Radii{radius, 0.0F, 0.0F, radius};
    }
    return Radii{radius, radius, 0.0F, 0.0F};
  }

} // namespace attached_panel
