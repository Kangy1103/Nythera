#pragma once

#include "render/core/texture_handle.h"

#include <array>

// Borrowed GPU image plus its logical sampling/ownership rectangles. Panel
// lifetime owns the image; frame and rounded-rectangle draws only reference it.
struct ScreenFrameBackdrop {
  TextureId texture;
  float opacity = 1.0F;
  std::array<float, 4> source{};
  std::array<float, 4> bounds{};
  bool operator==(const ScreenFrameBackdrop&) const = default;
};
