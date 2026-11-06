#pragma once

#include "config/config_types.h"
#include "render/core/screen_frame_geometry.h"
#include "shell/bar/screen_wrap_geometry.h"
#include "wayland/layer_surface.h"

#include <array>
#include <memory>

// Per-bar contribution handle. Sibling handles on one output share the four
// decorative buffers and one reservation per edge; outputs never share state.
class ScreenWrap {
public:
  ScreenWrap(
      WaylandConnection&, RenderContext*, wl_output*, const BarConfig&, int width, int height,
      ScreenWrap* sibling = nullptr, std::size_t priority = 0
  );
  ~ScreenWrap();
  void refreshPalette();
  void setOpacity(float);
  void setLayer(LayerShellLayer);
  void setReservation(int);
  void setIslands(const std::array<ScreenFrameSpan, 3>&, float depth = -1.0F);
  const ScreenFrameContour& contour() const;
  ScreenFrameStyle style() const;
  void setForegroundHole(InputRect);
  std::vector<InputRect> blurRegion(const screen_wrap::Rect&) const;

private:
  struct Shared;
  std::shared_ptr<Shared> m_shared;
  std::size_t m_id;
};
