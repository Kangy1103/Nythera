#include "shell/bar/screen_wrap.h"

#include "core/ui_phase.h"
#include "render/scene/screen_corner_node.h"
#include "render/scene/screen_frame_node.h"
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
  const bool islands = config.surfaceMode != "full";
  m_contour.width = static_cast<float>(width);
  m_contour.height = static_cast<float>(height);
  m_contour.rail = static_cast<float>(config.wrapWidth);
  m_contour.radius = static_cast<float>(m_geometry.radius);
  m_contour.depth = static_cast<float>(config.thickness);
  m_contour.islandRadius = std::min(static_cast<float>(config.radius), m_contour.depth / 2);
  m_contour.join = 2.0F * std::min(m_contour.islandRadius, (m_contour.depth - m_contour.rail) / 2);
  m_contour.edge = config.position == "right" ? 1 : config.position == "bottom" ? 2 : config.position == "left" ? 3 : 0;
  if (islands) {
    screen_wrap::reserveIslandSpace(
        m_geometry, config.position, config.thickness + static_cast<int>(std::ceil(m_contour.join)) + 1
    );
  }
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
  if (m_config.surfaceMode != "full") {
    auto root = std::make_unique<ScreenFrameNode>();
    root->setSize(static_cast<float>(rect.width), static_cast<float>(rect.height));
    root->setStyle(frameStyle(rect));
    root->setHitTestVisible(false);
    root->setOpacity(m_opacity);
    strip.root = std::move(root);
    strip.surface->setSceneRoot(strip.root.get());
    syncBlur(strip, rect);
    return;
  }
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
  auto regions = blurRegion(rect);
  if (m_foregroundHole.width > 0) {
    std::vector<InputRect> clipped;
    const InputRect hole{
        m_foregroundHole.x - rect.x, m_foregroundHole.y - rect.y, m_foregroundHole.width, m_foregroundHole.height
    };
    for (const auto r : regions) {
      const int left = std::max(r.x, hole.x), top = std::max(r.y, hole.y);
      const int right = std::min(r.x + r.width, hole.x + hole.width);
      const int bottom = std::min(r.y + r.height, hole.y + hole.height);
      if (right <= left || bottom <= top) {
        clipped.push_back(r);
        continue;
      }
      if (top > r.y)
        clipped.push_back({r.x, r.y, r.width, top - r.y});
      if (bottom < r.y + r.height)
        clipped.push_back({r.x, bottom, r.width, r.y + r.height - bottom});
      if (left > r.x)
        clipped.push_back({r.x, top, left - r.x, bottom - top});
      if (right < r.x + r.width)
        clipped.push_back({right, top, r.x + r.width - right, bottom - top});
    }
    regions = std::move(clipped);
  }
  strip.surface->setBlurRegion(regions);
}

std::vector<InputRect> ScreenWrap::blurRegion(const screen_wrap::Rect& rect) const {
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
  for (const auto& island : m_islandBlur) {
    const int x = std::max(island.x, rect.x), y = std::max(island.y, rect.y);
    const int right = std::min(island.x + island.width, rect.x + rect.width);
    const int bottom = std::min(island.y + island.height, rect.y + rect.height);
    if (right > x && bottom > y)
      regions.push_back({x - rect.x, y - rect.y, right - x, bottom - y});
  }
  return regions;
}

ScreenFrameStyle ScreenWrap::frameStyle(const screen_wrap::Rect& rect) const {
  return {
      m_contour,
      colorForRole(ColorRole::Surface, m_config.backgroundOpacity),
      resolveColorSpec(m_config.border),
      m_config.borderWidth,
      static_cast<float>(rect.x),
      static_cast<float>(rect.y),
      {static_cast<float>(m_foregroundHole.x), static_cast<float>(m_foregroundHole.y),
       static_cast<float>(m_foregroundHole.width), static_cast<float>(m_foregroundHole.height)}
  };
}

void ScreenWrap::setIslands(const std::array<ScreenFrameSpan, 3>& spans, float depth) {
  if (m_config.surfaceMode == "full")
    return;
  depth = depth < 0 ? static_cast<float>(m_config.thickness) : depth;
  if (spans == m_islands && depth == m_contour.depth)
    return;
  m_contour.depth = depth;
  m_islands = spans;
  const bool vertical = m_contour.edge == 1 || m_contour.edge == 3;
  m_contour.islands = joinedScreenFrameSpans(
      spans, vertical ? m_contour.height : m_contour.width, static_cast<float>(m_config.wrapJoinDistance),
      m_contour.depth
  );
  rebuildIslandBlur();
  for (std::size_t i = 0; i < m_strips.size(); ++i) {
    auto& strip = m_strips[i];
    if (!strip.root)
      continue;
    static_cast<ScreenFrameNode*>(strip.root.get())->setStyle(frameStyle(m_geometry.strips[i]));
    syncBlur(strip, m_geometry.strips[i]);
    strip.surface->requestRedraw();
  }
}

void ScreenWrap::rebuildIslandBlur() {
  m_islandBlur.clear();
  if (!m_config.compositorBlur)
    return;
  const bool vertical = m_contour.edge == 1 || m_contour.edge == 3;
  const float length = vertical ? m_contour.height : m_contour.width;
  const float cross = vertical ? m_contour.width : m_contour.height;
  const float limit = std::min(cross / 2, m_contour.depth + m_contour.join + 1);
  for (int u = 0; u < static_cast<int>(length); ++u) {
    const float column = static_cast<float>(u);
    const bool nearIsland = std::ranges::any_of(m_contour.islands, [&](const auto span) {
      return span.end > span.begin && column + 1 > span.begin - m_contour.join && column < span.end + m_contour.join;
    });
    if (!nearIsland)
      continue;
    float lo = 0, hi = limit;
    for (int step = 0; step < 9; ++step) {
      const float v = (lo + hi) / 2;
      const float main = static_cast<float>(u) + 0.5F;
      const float x = m_contour.edge == 1 ? cross - v : vertical ? v : main;
      const float y = m_contour.edge == 2 ? cross - v : vertical ? main : v;
      if (screenFrameDistance(m_contour, x, y) < 0)
        lo = v;
      else
        hi = v;
    }
    const int depth = static_cast<int>(std::ceil(lo));
    if (m_contour.edge == 0)
      m_islandBlur.push_back({u, 0, 1, depth});
    else if (m_contour.edge == 1)
      m_islandBlur.push_back({static_cast<int>(cross) - depth, u, depth, 1});
    else if (m_contour.edge == 2)
      m_islandBlur.push_back({u, static_cast<int>(cross) - depth, 1, depth});
    else
      m_islandBlur.push_back({0, u, depth, 1});
  }
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

void ScreenWrap::setForegroundHole(InputRect rect) {
  if (rect == m_foregroundHole)
    return;
  m_foregroundHole = rect;
  for (std::size_t i = 0; i < m_strips.size(); ++i) {
    auto& strip = m_strips[i];
    if (!strip.root)
      continue;
    static_cast<ScreenFrameNode*>(strip.root.get())->setStyle(frameStyle(m_geometry.strips[i]));
    syncBlur(strip, m_geometry.strips[i]);
    strip.surface->requestRedraw();
  }
}
