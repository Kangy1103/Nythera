#pragma once

struct SurfaceHazeStyle {
  bool enabled = false;
  bool transparent = false;
  float time = 0;
  float seed = 0;
  float opacity = 1;
  bool operator==(const SurfaceHazeStyle&) const = default;
};
