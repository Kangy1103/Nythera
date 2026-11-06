#include "shell/bar/screen_wrap.h"

#include "core/log.h"
#include "core/ui_phase.h"
#include "render/scene/screen_frame_node.h"
#include "shell/bar/wrap_layout.h"
#include "ui/builders.h"
#include "ui/palette.h"
#include "wayland/wayland_connection.h"

#include <cmath>
#include <map>

struct ScreenWrap::Shared {
  struct Contribution {
    BarConfig config;
    std::array<ScreenFrameSpan, 3> spans{};
    float depth = 0, opacity = 1;
    LayerShellLayer layer = LayerShellLayer::Top;
    int reservation = 0;
    InputRect hole{};
  };
  struct Strip {
    std::unique_ptr<LayerSurface> surface;
    std::unique_ptr<Node> root;
    screen_wrap::Rect rect{};
  };
  WaylandConnection& wayland;
  RenderContext* renderer;
  wl_output* output;
  int width, height;
  std::map<std::size_t, Contribution> bars;
  std::array<Strip, 4> strips, reservations;
  ScreenFrameStyle material;

  Shared(WaylandConnection& w, RenderContext* r, wl_output* o, int x, int y)
      : wayland(w), renderer(r), output(o), width(x), height(y) {}

  std::vector<InputRect> blurRegion(const screen_wrap::Rect& rect) const {
    std::vector<InputRect> result;
    // This fallback is only needed for compositors advertising client blur
    // regions. Umbriel uses its layer rules and never pays for this tessellation.
    if (!wayland.hasBackgroundEffectBlur())
      return result;
    for (int y = 0; y < rect.height; ++y) {
      int start = -1;
      for (int x = 0; x <= rect.width; ++x) {
        const float gx = static_cast<float>(rect.x + x) + 0.5F;
        const float gy = static_cast<float>(rect.y + y) + 0.5F;
        const auto& h = material.exclusion;
        const bool excluded = h[2] > 0 && gx >= h[0] && gy >= h[1] && gx < h[0] + h[2] && gy < h[1] + h[3];
        const bool inside = x < rect.width && !excluded && screenFrameDistance(material.contour, gx, gy) < 0;
        if (inside && start < 0)
          start = x;
        if (!inside && start >= 0) {
          result.push_back({start, y, x - start, 1});
          start = -1;
        }
      }
    }
    return result;
  }

  void buildStrip(std::size_t i) {
    auto& strip = strips[i];
    if (!strip.surface)
      return;
    if (!strip.root) {
      strip.root = std::make_unique<ScreenFrameNode>();
      strip.root->setHitTestVisible(false);
      strip.surface->setSceneRoot(strip.root.get());
    }
    auto* node = static_cast<ScreenFrameNode*>(strip.root.get());
    node->setSize(static_cast<float>(strip.rect.width), static_cast<float>(strip.rect.height));
    auto style = material;
    style.offsetX = static_cast<float>(strip.rect.x);
    style.offsetY = static_cast<float>(strip.rect.y);
    node->setStyle(style);

    strip.surface->setInputRegion({});
    const bool blur =
        std::ranges::any_of(bars, [](const auto& p) { return p.second.config.compositorBlur && p.second.opacity > 0; });
    if (blur && wayland.hasBackgroundEffectBlur())
      strip.surface->setBlurRegion(blurRegion(strip.rect));
    else
      strip.surface->clearBlurRegion();
    strip.surface->requestRedraw();
  }

  void reserveEdges() {
    std::vector<BarConfig> configs;
    std::vector<int> requests;
    for (const auto& [id, bar] : bars) {
      configs.push_back(bar.config);
      requests.push_back(bar.reservation);
    }
    const auto requested = screen_wrap::reservations(configs, requests);
    const auto zones = screen_wrap::boundedReservations(requested, width, height);
    if (zones != requested) {
      static constexpr Logger log("screen-wrap");
      log.warn("requested frame clearance cannot fit output {}x{}; work-area reservations were bounded", width, height);
    }
    for (std::size_t i = 0; i < zones.size(); ++i) {
      auto& strip = reservations[i];
      if (!strip.surface && zones[i] > 0) {
        const bool side = i == 1 || i == 3;
        const auto edge = i == 0 ? LayerShellAnchor::Top
            : i == 1             ? LayerShellAnchor::Right
            : i == 2             ? LayerShellAnchor::Bottom
                                 : LayerShellAnchor::Left;
        const auto anchor = edge
            | (side ? LayerShellAnchor::Top | LayerShellAnchor::Bottom
                    : LayerShellAnchor::Left | LayerShellAnchor::Right);
        strip.surface = std::make_unique<LayerSurface>(
            wayland,
            LayerSurfaceConfig{
                .nameSpace = "nythera-bar-reservation-" + std::to_string(i),
                .layer = LayerShellLayer::Top,
                .anchor = anchor,
                .width = side ? 1U : 0U,
                .height = side ? 0U : 1U,
                .exclusiveZone = zones[i],
                .defaultWidth = side ? 1U : static_cast<unsigned>(width),
                .defaultHeight = side ? static_cast<unsigned>(height) : 1U
            }
        );
        strip.surface->setClickThrough(true);
        strip.surface->setRenderContext(renderer);
        strip.surface->setConfigureCallback([this, i](std::uint32_t w, std::uint32_t h) {
          auto& s = reservations[i];
          s.root = ui::node({});
          s.root->setSize(static_cast<float>(w), static_cast<float>(h));
          s.root->setHitTestVisible(false);
          s.surface->setSceneRoot(s.root.get());
          s.surface->setInputRegion({});
        });
        if (!strip.surface->initialize(output))
          strip.surface.reset();
      }
      if (strip.surface)
        strip.surface->setExclusiveZone(zones[i]);
    }
  }

  void refresh() {
    if (bars.empty())
      return;
    UiPhaseScope phase(UiPhase::Layout);
    const auto& primary = bars.begin()->second;
    float visible = 0;
    for (const auto& [id, b] : bars)
      visible = std::max(visible, b.opacity);
    int rail = 1, radius = 0, outline = 0;
    auto layer = primary.layer;
    for (const auto& [id, b] : bars) {
      rail = std::max(rail, b.config.wrapWidth);
      radius = std::max(radius, b.config.wrapRadius);
      outline = std::max(outline, static_cast<int>(std::ceil(b.config.borderWidth)));
      if (static_cast<unsigned>(b.layer) < static_cast<unsigned>(layer))
        layer = b.layer;
    }
    auto geometry = screen_wrap::geometry(width, height, rail, radius, 0, "top", false, outline);
    material = {};
    material.contour.width = static_cast<float>(width);
    material.contour.height = static_cast<float>(height);
    material.contour.rail = static_cast<float>(geometry.left);
    material.contour.radius = static_cast<float>(geometry.radius);
    material.fill = colorForRole(ColorRole::Surface, primary.config.backgroundOpacity * visible);
    material.border = resolveColorSpec(primary.config.border);
    material.border.a *= visible;
    material.borderWidth = primary.config.borderWidth;
    for (const auto& [id, b] : bars) {
      const auto& c = b.config;
      const float stableDepth = static_cast<float>(c.marginEdge + c.thickness);
      const float rounding = std::min(static_cast<float>(c.radius), static_cast<float>(c.thickness) / 2);
      const float join = 2 * std::min(rounding, (static_cast<float>(c.thickness) - static_cast<float>(rail)) / 2);
      screen_wrap::reserveIslandSpace(
          geometry, c.position, static_cast<int>(std::ceil(stableDepth + std::max(0.0F, join))) + 1
      );
      if (b.opacity <= 0)
        continue;
      ScreenFrameBand band;
      band.name = c.name;
      band.edge = screen_wrap::edgeIndex(c.position);
      band.outer = static_cast<float>(c.marginEdge);
      band.depth = band.outer + b.depth;
      band.radius = rounding;
      band.join = std::max(0.0F, join);
      band.spans = b.spans;
      band.fill = colorForRole(ColorRole::Surface, c.backgroundOpacity * b.opacity);
      band.border = resolveColorSpec(c.border);
      band.border.a *= b.opacity;
      band.borderWidth = c.borderWidth;
      if (c.surfaceMode == "full") {
        const float length = band.edge == 1 || band.edge == 3 ? static_cast<float>(height) : static_cast<float>(width);
        band.spans = {ScreenFrameSpan{-stableDepth, length + stableDepth}, {}, {}};
      } else {
        band.spans = joinedScreenFrameSpans(
            band.spans, band.edge == 1 || band.edge == 3 ? static_cast<float>(height) : static_cast<float>(width),
            static_cast<float>(c.wrapJoinDistance), stableDepth
        );
      }
      material.contour.bands.push_back(std::move(band));
      if (b.hole.width > 0)
        material.exclusion = {
            static_cast<float>(b.hole.x), static_cast<float>(b.hole.y), static_cast<float>(b.hole.width),
            static_cast<float>(b.hole.height)
        };
    }
    for (std::size_t i = 0; i < strips.size(); ++i) {
      auto& strip = strips[i];
      const auto next = geometry.strips[i];
      const bool moved = strip.rect.x != next.x
          || strip.rect.y != next.y
          || strip.rect.width != next.width
          || strip.rect.height != next.height;
      strip.rect = next;
      if (!strip.surface) {
        strip.surface = std::make_unique<LayerSurface>(
            wayland,
            LayerSurfaceConfig{
                .nameSpace = "nythera-bar-wrap-shared",
                .layer = layer,
                .anchor = LayerShellAnchor::Top | LayerShellAnchor::Left,
                .width = static_cast<unsigned>(strip.rect.width),
                .height = static_cast<unsigned>(strip.rect.height),
                .exclusiveZone = -1,
                .marginTop = strip.rect.y,
                .marginLeft = strip.rect.x,
                .defaultWidth = static_cast<unsigned>(strip.rect.width),
                .defaultHeight = static_cast<unsigned>(strip.rect.height)
            }
        );
        strip.surface->setClickThrough(true);
        strip.surface->setRenderContext(renderer);
        strip.surface->setConfigureCallback([this, i](std::uint32_t, std::uint32_t) { buildStrip(i); });
        if (!strip.surface->initialize(output))
          strip.surface.reset();
      } else {
        strip.surface->setLayer(layer);
        if (moved) {
          strip.surface->setMargins(strip.rect.y, 0, 0, strip.rect.x);
          strip.surface->requestSize(static_cast<unsigned>(strip.rect.width), static_cast<unsigned>(strip.rect.height));
        }
      }
      buildStrip(i);
    }
  }
};

ScreenWrap::ScreenWrap(
    WaylandConnection& w, RenderContext* r, wl_output* o, const BarConfig& config, int width, int height,
    ScreenWrap* sibling, std::size_t priority
)
    : m_shared(sibling ? sibling->m_shared : std::make_shared<Shared>(w, r, o, width, height)), m_id(priority) {
  auto& entry = m_shared->bars[m_id];
  entry.config = config;
  entry.depth = static_cast<float>(config.thickness);
  entry.layer = layerShellLayerFromConfig(config.layer);
  entry.reservation = config.reserveSpace ? config.marginEdge + config.thickness + config.marginOppositeEdge : 0;
  m_shared->refresh();
  m_shared->reserveEdges();
}
ScreenWrap::~ScreenWrap() {
  m_shared->bars.erase(m_id);
  if (!m_shared->bars.empty()) {
    m_shared->refresh();
    m_shared->reserveEdges();
  }
}
void ScreenWrap::refreshPalette() { m_shared->refresh(); }
void ScreenWrap::setOpacity(float opacity) {
  auto& b = m_shared->bars.at(m_id);
  if (b.opacity == opacity)
    return;
  b.opacity = opacity;
  m_shared->refresh();
}
void ScreenWrap::setLayer(LayerShellLayer layer) {
  auto& b = m_shared->bars.at(m_id);
  if (b.layer == layer)
    return;
  b.layer = layer;
  m_shared->refresh();
}
void ScreenWrap::setReservation(int value) {
  auto& b = m_shared->bars.at(m_id);
  if (b.reservation == value)
    return;
  b.reservation = value;
  m_shared->reserveEdges();
}
void ScreenWrap::setIslands(const std::array<ScreenFrameSpan, 3>& spans, float depth) {
  auto& b = m_shared->bars.at(m_id);
  if (depth < 0)
    depth = static_cast<float>(b.config.thickness);
  if (b.spans == spans && b.depth == depth)
    return;
  b.spans = spans;
  b.depth = depth;
  m_shared->refresh();
}
const ScreenFrameContour& ScreenWrap::contour() const { return m_shared->material.contour; }
ScreenFrameStyle ScreenWrap::style() const { return m_shared->material; }
void ScreenWrap::setForegroundHole(InputRect rect) {
  auto& b = m_shared->bars.at(m_id);
  if (b.hole.x == rect.x && b.hole.y == rect.y && b.hole.width == rect.width && b.hole.height == rect.height)
    return;
  b.hole = rect;
  m_shared->refresh();
}
std::vector<InputRect> ScreenWrap::blurRegion(const screen_wrap::Rect& rect) const {
  return m_shared->blurRegion(rect);
}
