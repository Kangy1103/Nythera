#include "shell/bar/screen_wrap.h"

#include "core/ui_phase.h"
#include "render/scene/screen_corner_node.h"
#include "ui/builders.h"
#include "ui/controls/screen_corner.h"
#include "ui/palette.h"

#include <cmath>

ScreenWrap::ScreenWrap(
    WaylandConnection& wayland, RenderContext* renderer, wl_output* output, const BarConfig& config, int width,
    int height
)
    : m_config(config), m_geometry(
                            screen_wrap::geometry(
                                width, height, config.wrapWidth, config.wrapRadius, config.thickness, config.position,
                                config.surfaceMode == "full", static_cast<int>(std::ceil(config.borderWidth))
                            )
                        ) {
  for (std::size_t i = 0; i < m_strips.size(); ++i) {
    const auto rect = m_geometry.strips[i];
    if (rect.width <= 0 || rect.height <= 0)
      continue;
    auto& strip = m_strips[i];
    strip.surface = std::make_unique<LayerSurface>(
        wayland,
        LayerSurfaceConfig{
            .nameSpace = "cyberpunk-screen-wrap-" + config.name,
            .layer = layerShellLayerFromConfig(config.layer),
            .anchor = LayerShellAnchor::Top | LayerShellAnchor::Left,
            .width = static_cast<std::uint32_t>(rect.width),
            .height = static_cast<std::uint32_t>(rect.height),
            // Ignore existing exclusive zones: these are physical screen edges.
            .exclusiveZone = -1,
            .marginTop = rect.y,
            .marginLeft = rect.x,
            .defaultWidth = static_cast<std::uint32_t>(rect.width),
            .defaultHeight = static_cast<std::uint32_t>(rect.height),
            .prewarmBlur = config.compositorBlur,
        }
    );
    strip.surface->setRenderContext(renderer);
    auto* part = &strip;
    strip.surface->setConfigureCallback([this, part, rect](std::uint32_t, std::uint32_t) {
      UiPhaseScope phase(UiPhase::Layout);
      build(*part, rect);
    });
    if (!strip.surface->initialize(output)) {
      strip.surface.reset();
      continue;
    }
    strip.surface->setInputRegion({});
  }
}

void ScreenWrap::build(Strip& strip, const screen_wrap::Rect& rect) {
  const auto& g = m_geometry;
  auto root = ui::node({});
  root->setSize(static_cast<float>(rect.width), static_cast<float>(rect.height));
  root->setClipChildren(true);
  root->setHitTestVisible(false);
  root->setOpacity(m_opacity);
  const auto fill = colorForRole(ColorRole::Surface, m_config.backgroundOpacity);
  auto addFill = [&](int x, int y, int w, int h) {
    auto box = ui::box();
    box->setPosition(static_cast<float>(x - rect.x), static_cast<float>(y - rect.y));
    box->setSize(static_cast<float>(w), static_cast<float>(h));
    box->setFill(fill);
    root->addChild(std::move(box));
  };
  addFill(0, 0, g.width, g.top);
  addFill(0, g.height - g.bottom, g.width, g.bottom);
  addFill(0, g.top, g.left, g.height - g.top - g.bottom);
  addFill(g.width - g.right, g.top, g.right, g.height - g.top - g.bottom);
  if (g.radius > 0) {
    const int xs[] = {g.left, g.width - g.right - g.radius, g.width - g.right - g.radius, g.left};
    const int ys[] = {g.top, g.top, g.height - g.bottom - g.radius, g.height - g.bottom - g.radius};
    for (int i = 0; i < 4; ++i) {
      auto corner = std::make_unique<ScreenCorner>();
      corner->setPosition(static_cast<float>(xs[i] - rect.x), static_cast<float>(ys[i] - rect.y));
      corner->setSize(static_cast<float>(g.radius), static_cast<float>(g.radius));
      corner->setCorner(static_cast<ScreenCornerPosition>(i));
      corner->setExponent(2.0F);
      corner->setColor(fill);
      root->addChild(std::move(corner));
    }
  }
  if (m_config.borderWidth > 0.0F) {
    auto outline = ui::box();
    outline->setPosition(static_cast<float>(g.left - rect.x), static_cast<float>(g.top - rect.y));
    outline->setSize(static_cast<float>(g.width - g.left - g.right), static_cast<float>(g.height - g.top - g.bottom));
    outline->setFill(Color{0, 0, 0, 0});
    outline->setBorder(resolveColorSpec(m_config.border), m_config.borderWidth);
    outline->setRadius(static_cast<float>(g.radius));
    root->addChild(std::move(outline));
  }
  strip.root = std::move(root);
  strip.surface->setSceneRoot(strip.root.get());
  syncBlur(strip, rect);
}

void ScreenWrap::syncBlur(Strip& strip, const screen_wrap::Rect& rect) {
  if (!m_config.compositorBlur || m_opacity <= 0.0F) {
    strip.surface->clearBlurRegion();
    return;
  }
  // Blur only the painted frame, never the transparent desktop opening.
  const auto& g = m_geometry;
  std::vector<InputRect> regions;
  auto add = [&](int begin, int end, int y) {
    begin = std::max(begin, rect.x);
    end = std::min(end, rect.x + rect.width);
    if (end > begin)
      regions.push_back({begin - rect.x, y - rect.y, end - begin, 1});
  };
  for (int y = rect.y; y < rect.y + rect.height; ++y) {
    if (y < g.top || y >= g.height - g.bottom) {
      add(0, g.width, y);
      continue;
    }
    const float cornerY = std::max(
        static_cast<float>(g.top + g.radius) - (static_cast<float>(y) + 0.5F),
        static_cast<float>(y) + 0.5F - static_cast<float>(g.height - g.bottom - g.radius)
    );
    const int carve = cornerY > 0.0F
        ? static_cast<int>(std::ceil(
              static_cast<float>(g.radius)
              - std::sqrt(std::max(0.0F, static_cast<float>(g.radius * g.radius) - cornerY * cornerY))
          ))
        : 0;
    add(0, g.left + carve, y);
    add(g.width - g.right - carve, g.width, y);
  }
  strip.surface->setBlurRegion(regions);
}

void ScreenWrap::refreshPalette() {
  UiPhaseScope phase(UiPhase::Layout);
  for (std::size_t i = 0; i < m_strips.size(); ++i) {
    auto& strip = m_strips[i];
    if (!strip.surface)
      continue;
    build(strip, m_geometry.strips[i]);
    strip.surface->requestRedraw();
  }
}

void ScreenWrap::setOpacity(float opacity) {
  if (m_opacity == opacity)
    return;
  const bool blurChanged = (m_opacity <= 0.0F) != (opacity <= 0.0F);
  m_opacity = opacity;
  for (std::size_t i = 0; i < m_strips.size(); ++i) {
    auto& strip = m_strips[i];
    if (!strip.root)
      continue;
    strip.root->setOpacity(opacity);
    if (blurChanged)
      syncBlur(strip, m_geometry.strips[i]);
    strip.surface->requestRedraw();
  }
}

void ScreenWrap::setLayer(LayerShellLayer layer) {
  for (auto& strip : m_strips) {
    if (strip.surface)
      strip.surface->setLayer(layer);
  }
}
