#include "shell/panel/panel_snapshot.h"
#include "capture/screencopy_util.h"

#include "core/log.h"
#include "render/backend/render_backend.h"
#include "render/core/texture_manager.h"
#include "render/render_context.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace { constexpr Logger kLog("panel-snapshot"); }

PanelSnapshot::PanelSnapshot(WaylandConnection& wayland, RenderContext& render) : m_capture(wayland), m_render(render) {}
PanelSnapshot::~PanelSnapshot() { clear(); }

void PanelSnapshot::clear() {
  m_timeout.stop();
  m_capture.cancelInFlight();
  m_ready = {};
  m_pending = false;
  m_backdrop = {};
  m_blur.destroy();
  if (m_source.valid() && m_render.backend().makeCurrentNoSurface())
    m_render.textureManager().unload(m_source);
}

void PanelSnapshot::finish() {
  m_timeout.stop();
  m_pending = false;
  if (auto ready = std::exchange(m_ready, {})) ready();
}

void PanelSnapshot::setStrength(float strength) {
  strength = std::clamp(strength, 0.0F, 20.0F);
  if (m_backdrop.texture.valid() && strength == m_strength) return;
  m_strength = strength;
  if (!m_source.valid()) return;
  const int divisor = strength > 0 ? 3 : 1;
  const auto blurred = m_blur.get(m_render.backend(), m_source,
      static_cast<std::uint32_t>(std::max(1, m_source.width / divisor)),
      static_cast<std::uint32_t>(std::max(1, m_source.height / divisor)), strength, 2);
  m_backdrop.texture = blurred.id;
}

void PanelSnapshot::capture(wl_output* output, LogicalRect region, float strength, std::function<void()> ready) {
  clear();
  m_ready = std::move(ready);
  m_strength = strength;
  if (!m_capture.available() || region.width <= 0 || region.height <= 0) { finish(); return; }
  m_pending = true;
  const auto started = std::chrono::steady_clock::now();
  m_timeout.start(std::chrono::milliseconds(200), [this] {
    m_capture.cancelInFlight();
    kLog.warn("capture timed out; retaining normal panel backdrop");
    finish();
  });
  m_capture.capture(output, region, false,
      [this, output, region, started](std::optional<ScreencopyImage> image, std::string error) {
    if (image && m_render.backend().makeCurrentNoSurface()) {
      screencopy::orientCaptureNative(*image, m_capture.wayland(), output);
      m_source = m_render.textureManager().loadFromRgba(image->rgba.data(), image->width, image->height);
      m_backdrop.source = {static_cast<float>(region.x), static_cast<float>(region.y),
          static_cast<float>(region.width), static_cast<float>(region.height)};
      setStrength(m_strength);
      kLog.info("{}x{} captured and blurred once in {} ms", image->width, image->height,
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
    } else kLog.warn("capture failed: {}; retaining normal panel backdrop", error);
    finish();
  });
}

LogicalRect panelSnapshotRegion(LogicalRect body, int outputWidth, int outputHeight, const ScreenFrameContour& frame) {
  const float inset = frame.rail >= 0 ? frame.rail + frame.radius : 0;
  float left = inset, top = inset, right = static_cast<float>(outputWidth) - inset, bottom = static_cast<float>(outputHeight) - inset;
  for (const auto& band : frame.bands) {
    if (band.name.ends_with(":panel")) continue; // other native tools are valid desktop content
    if (band.edge == 0) top = std::max(top, band.depth);
    else if (band.edge == 1) right = std::min(right, static_cast<float>(outputWidth) - band.depth);
    else if (band.edge == 2) bottom = std::min(bottom, static_cast<float>(outputHeight) - band.depth);
    else left = std::max(left, band.depth);
  }
  const int x = std::max(body.x, static_cast<int>(std::ceil(left)));
  const int y = std::max(body.y, static_cast<int>(std::ceil(top)));
  return {x, y, std::max(0, std::min(body.x + body.width, static_cast<int>(std::floor(right))) - x),
                std::max(0, std::min(body.y + body.height, static_cast<int>(std::floor(bottom))) - y)};
}
