#pragma once

#include "config/config_types.h"
#include "render/core/screen_frame_geometry.h"
#include "shell/bar/screen_wrap_geometry.h"
#include "wayland/layer_surface.h"

#include <array>
#include <memory>

// Decorative, click-through edge surfaces. Owned by the bar, so output removal,
// config rebuilds and shell shutdown cannot leave an orphan frame behind.
class ScreenWrap {
public:
  ScreenWrap(
      WaylandConnection& wayland, RenderContext* renderer, wl_output* output, const BarConfig& config, int width,
      int height
  );
  void refreshPalette();
  void setOpacity(float opacity);
  void setLayer(LayerShellLayer layer);
  void setIslands(const std::array<ScreenFrameSpan, 3>& spans);

private:
  struct Strip {
    std::unique_ptr<LayerSurface> surface;
    std::unique_ptr<Node> root;
  };
  void build(Strip& strip, const screen_wrap::Rect& rect);
  void syncBlur(Strip& strip, const screen_wrap::Rect& rect);
  ScreenFrameStyle frameStyle(const screen_wrap::Rect& rect) const;
  void rebuildIslandBlur();
  BarConfig m_config;
  screen_wrap::Geometry m_geometry;
  std::array<Strip, 4> m_strips;
  float m_opacity = 1.0F;
  ScreenFrameContour m_contour;
  std::array<ScreenFrameSpan, 3> m_islands{};
  std::vector<InputRect> m_islandBlur;
};
