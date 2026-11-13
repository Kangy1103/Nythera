#pragma once

#include "config/config_types.h"
#include "render/core/screen_frame_geometry.h"
#include "shell/bar/screen_wrap_geometry.h"
#include "wayland/layer_surface.h"

#include <array>
#include <memory>

class Wallpaper;
class CompositorPlatform;

// Per-bar contribution handle. Sibling handles on one output share the four
// decorative buffers and one reservation per edge; outputs never share state.
class ScreenWrap {
public:
  ScreenWrap(
      WaylandConnection&, RenderContext*, wl_output*, const BarConfig&, int width, int height,
      ScreenWrap* sibling = nullptr, std::size_t priority = 0, Wallpaper* wallpaper = nullptr
  );
  ~ScreenWrap();
  void refreshPalette();
  void updateWallpaperUnderlap(const CompositorPlatform&);
  void setOpacity(float);
  void setLayer(LayerShellLayer);
  void setReservation(int);
  void setIslands(const std::array<ScreenFrameSpan, 3>&, float depth = -1.0F);
  const ScreenFrameContour& contour() const;
  ScreenFrameStyle style() const;
  void setForegroundHole(InputRect);
  void setAttachedTool(
      std::optional<ScreenFrameSpan>, float depth = 0, float bufferDepth = 0, int islandIndex = -1,
      ScreenFrameBackdrop backdrop = {}, std::string hazeProfile = {}, std::array<float, 4> hazeBounds = {}
  );
  std::vector<InputRect> blurRegion(const screen_wrap::Rect&) const;

private:
  struct Shared;
  std::shared_ptr<Shared> m_shared;
  std::size_t m_id;
};
