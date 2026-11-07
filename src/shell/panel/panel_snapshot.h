#pragma once

#include "capture/screencopy_capture.h"
#include "core/timer_manager.h"
#include "render/core/blur_cache.h"
#include "render/core/screen_frame_geometry.h"

#include <functional>

class RenderContext;

// One capture per panel opening. Strength changes reblur the retained image;
// they never recapture the desktop or run a continuous capture stream.
class PanelSnapshot {
public:
  PanelSnapshot(WaylandConnection&, RenderContext&);
  ~PanelSnapshot();
  void capture(wl_output*, LogicalRect, float strength, std::function<void()> ready);
  void clear();
  void setStrength(float);
  bool pending() const { return m_pending; }
  ScreenFrameBackdrop backdrop() const { return m_backdrop; }

private:
  void finish();
  ScreencopyCapture m_capture;
  RenderContext& m_render;
  BlurCache m_blur;
  TextureHandle m_source;
  ScreenFrameBackdrop m_backdrop;
  Timer m_timeout;
  std::function<void()> m_ready;
  float m_strength = 12;
  bool m_pending = false;
};

// Trim occupied bar strips out of a capture. The backdrop sampler extends the
// remaining clean edge pixels into any neck, avoiding self-capture ghosts.
LogicalRect panelSnapshotRegion(LogicalRect body, int outputWidth, int outputHeight,
                               const ScreenFrameContour& frame = {});
