#include "shell/bar/screen_wrap.h"

#include "compositors/compositor_platform.h"
#include "core/log.h"
#include "core/files/resource_paths.h"
#include "core/ui_phase.h"
#include "core/timer_manager.h"
#include "render/scene/screen_frame_node.h"
#include "render/scene/wallpaper_node.h"
#include "shell/bar/wrap_layout.h"
#include "shell/wallpaper/wallpaper.h"
#include "ui/builders.h"
#include "ui/controls/image.h"
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
    std::optional<ScreenFrameSpan> toolSpan;
    int toolIslandIndex = -1;
    ScreenFrameBackdrop backdrop;
    std::string hazeProfile;
    std::array<float, 4> hazeBounds{};
    float toolDepth = 0, toolBufferDepth = 0;
  };
  struct Strip {
    std::unique_ptr<LayerSurface> surface;
    std::unique_ptr<Node> root;
    ScreenFrameNode* frame = nullptr;
    std::array<Box*, 4> backingClips{};
    std::array<WallpaperNode*, 4> wallpapers{};
    Image* cornerLogo = nullptr;
    screen_wrap::Rect rect{};
  };
  WaylandConnection& wayland;
  RenderContext* renderer;
  wl_output* output;
  int width, height;
  std::map<std::size_t, Contribution> bars;
  std::array<Strip, 4> strips, reservations;
  ScreenFrameStyle material;
  Wallpaper* wallpaper;
  std::optional<WallpaperDrawParams> wallpaperSource;
  Signal<>::ScopedConnection wallpaperChanged;
  Signal<wl_output*>::ScopedConnection wallpaperTransitionChanged;
  bool backingLogged = false;
  bool leftWallpaperUnderlap = false;
  Timer backingRestoreTimer;

  void setLeftWallpaperUnderlap(bool underlap) {
    leftWallpaperUnderlap = underlap;
    for (auto& strip : strips) {
      if (!strip.root) continue;
      buildBacking(strip);
      strip.surface->requestRedraw();
    }
  }

  Shared(WaylandConnection& w, RenderContext* r, wl_output* o, int x, int y, Wallpaper* wp)
      : wayland(w), renderer(r), output(o), width(x), height(y), wallpaper(wp) {
    if (wallpaper) {
      wallpaperSource = wallpaper->backingSource(output);
      wallpaperChanged = wallpaper->changed().connect([this] {
        wallpaperSource = wallpaper->backingSource(output);
        for (auto& strip : strips) {
          if (!strip.root) continue;
          buildBacking(strip);
          strip.surface->requestRedraw();
        }
      });
      wallpaperTransitionChanged = wallpaper->backingTransitionChanged().connect([this](wl_output* changedOutput) {
        if (changedOutput != output) return;
        wallpaperSource = wallpaper->backingSource(output);
        if (std::ranges::none_of(bars, [](const auto& entry) {
              return entry.second.config.wrapWallpaperBacking && entry.second.opacity > 0.0F;
            })) return;
        for (auto& strip : strips) {
          if (!strip.root) continue;
          buildBacking(strip);
          strip.surface->requestRedraw();
        }
      });
    }
  }

  void buildBacking(Strip& strip) {
    std::array<int, 4> depths{};
    std::array<float, 4> opacities{};
    for (const auto& [id, bar] : bars) {
      if (!bar.config.wrapWallpaperBacking || !bar.config.enabled || bar.config.position == "top") continue;
      const auto edge = screen_wrap::edgeIndex(bar.config.position);
      const int extension = edge == 3 ? screen_wrap::leftBackingExtension : 0;
      depths[edge] = std::max(depths[edge], bar.config.marginEdge + bar.config.thickness + extension);
      opacities[edge] = std::max(opacities[edge], bar.opacity);
    }
    const auto rects = screen_wrap::wallpaperBackingRects(width, height, depths);
    if (leftWallpaperUnderlap) opacities[3] = 0;
    if (!backingLogged && wallpaperSource && std::ranges::any_of(depths, [](int depth) { return depth > 0; })) {
      static constexpr Logger log("screen-wrap");
      log.info("using resident wallpaper backing on {}x{} (no capture or blur)", width, height);
      backingLogged = true;
    }
    for (std::size_t e = 0; e < rects.size(); ++e) {
      auto* clip = strip.backingClips[e];
      const auto& rect = rects[e];
      const int x = std::max(rect.x, strip.rect.x), y = std::max(rect.y, strip.rect.y);
      const int w = std::min(rect.x + rect.width, strip.rect.x + strip.rect.width) - x;
      const int h = std::min(rect.y + rect.height, strip.rect.y + strip.rect.height) - y;
      clip->setVisible(wallpaperSource.has_value() && w > 0 && h > 0 && opacities[e] > 0);
      if (!clip->visible()) continue;
      clip->setPosition(static_cast<float>(x - strip.rect.x), static_cast<float>(y - strip.rect.y));
      clip->setSize(static_cast<float>(w), static_cast<float>(h));
      clip->setOpacity(opacities[e]);
      const auto& source = *wallpaperSource;
      auto fill = source.fillColor;
      fill.a = 1.0F;
      clip->setStyle(RoundedRectStyle{.fill = fill, .fillMode = FillMode::Solid});
      auto* node = strip.wallpapers[e];
      node->setPosition(static_cast<float>(-x), static_cast<float>(-y));
      node->setSize(static_cast<float>(width), static_cast<float>(height));
      node->setSources(source.from.kind, source.from.texture, source.from.color,
          source.to.kind, source.to.texture, source.to.color,
          source.from.imageWidth, source.from.imageHeight, source.to.imageWidth, source.to.imageHeight);
      node->setTransition(source.transition, source.progress, source.params);
      node->setFillMode(static_cast<WallpaperFillMode>(source.fillMode));
      node->setFillColor(fill);
      node->setSpan(source.span);
    }
  }

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
      strip.root = std::make_unique<Node>();
      strip.root->setHitTestVisible(false);
      for (std::size_t e = 0; e < strip.backingClips.size(); ++e) {
        auto clip = ui::box({});
        clip->setClipChildren(true);
        clip->setHitTestVisible(false);
        auto image = std::make_unique<WallpaperNode>();
        image->setHitTestVisible(false);
        strip.wallpapers[e] = static_cast<WallpaperNode*>(clip->addChild(std::move(image)));
        strip.backingClips[e] = static_cast<Box*>(strip.root->addChild(std::move(clip)));
      }
      auto frame = std::make_unique<ScreenFrameNode>();
      frame->setHitTestVisible(false);
      strip.frame = static_cast<ScreenFrameNode*>(strip.root->addChild(std::move(frame)));
      if (i == 0) {
        auto logo = ui::image({});
        logo->setHitTestVisible(false);
        logo->setFit(ImageFit::Contain);
        strip.cornerLogo = static_cast<Image*>(strip.root->addChild(std::move(logo)));
      }
      strip.surface->setSceneRoot(strip.root.get());
    }
    strip.root->setSize(static_cast<float>(strip.rect.width), static_cast<float>(strip.rect.height));
    buildBacking(strip);
    auto* node = strip.frame;
    node->setSize(static_cast<float>(strip.rect.width), static_cast<float>(strip.rect.height));
    auto style = material;
    style.offsetX = static_cast<float>(strip.rect.x);
    style.offsetY = static_cast<float>(strip.rect.y);
    node->setStyle(style);
    strip.surface->setHazeProfile(style.hazeProfile);

    if (strip.cornerLogo) {
      std::vector<BarConfig> visibleBars;
      float topOpacity = 0, leftOpacity = 0;
      for (const auto& [id, bar] : bars) {
        if (bar.opacity <= 0.001F) continue;
        visibleBars.push_back(bar.config);
        if (bar.config.position == "top") topOpacity = std::max(topOpacity, bar.opacity);
        if (bar.config.position == "left") leftOpacity = std::max(leftOpacity, bar.opacity);
      }
      const auto logo = screen_wrap::topLeftBrandingRect(visibleBars);
      const bool logoResized = strip.cornerLogo->width() != static_cast<float>(logo.width);
      strip.cornerLogo->setVisible(logo.width > 0 && screen_wrap::hasTopLeftJoin(material.contour));
      strip.cornerLogo->setPosition(static_cast<float>(logo.x - strip.rect.x), static_cast<float>(logo.y - strip.rect.y));
      strip.cornerLogo->setSize(static_cast<float>(logo.width), static_cast<float>(logo.height));
      strip.cornerLogo->setOpacity(std::min(topOpacity, leftOpacity));
      // Shared frame surfaces otherwise request paint only. Their prepare
      // callback (which loads the SVG) is not run for a redraw-only frame.
      if (strip.cornerLogo->visible() && (logoResized || !strip.cornerLogo->hasImage())) strip.surface->requestLayout();
    }

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
    int rail = 1, radius = 0;
    const int outline = static_cast<int>(std::ceil(primary.config.borderWidth));
    auto layer = primary.layer;
    for (const auto& [id, b] : bars) {
      rail = std::max(rail, b.config.wrapWidth);
      radius = std::max(radius, b.config.wrapRadius);
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
          geometry, c.position, static_cast<int>(std::ceil(std::max(stableDepth, static_cast<float>(c.marginEdge) + b.toolBufferDepth) + std::max(0.0F, join))) + 1
      );
      if (c.enabled && c.wrapWallpaperBacking && c.position == "left")
        screen_wrap::reserveIslandSpace(geometry, c.position, c.marginEdge + c.thickness + screen_wrap::leftBackingExtension);
      if (b.opacity <= 0)
        continue;
      ScreenFrameBand band;
      band.name = c.name;
      band.edge = screen_wrap::edgeIndex(c.position);
      band.outer = static_cast<float>(c.marginEdge);
      band.depth = band.outer + b.depth;
      band.radius = rounding;
      band.join = std::max(0.0F, join);
      band.softChamfer = c.wrapChamfer;
      band.chamferUpper = static_cast<float>(c.wrapChamferUpper);
      band.chamferLower = static_cast<float>(c.wrapChamferLower);
      band.spans = b.spans;
      band.fill = colorForRole(ColorRole::Surface, c.backgroundOpacity * b.opacity);
      // Geometry and fill belong to each bar, but the union has one outline.
      // Mixing individual border widths/colours makes the perimeter disappear
      // or change colour where a borderless sibling meets the shared rails.
      band.border = material.border;
      band.borderWidth = material.borderWidth;
      if (c.surfaceMode == "full") {
        const float length = band.edge == 1 || band.edge == 3 ? static_cast<float>(height) : static_cast<float>(width);
        band.spans = {ScreenFrameSpan{-stableDepth, length + stableDepth}, {}, {}};
      } else {
        band.spans = joinedScreenFrameSpans(
            band.spans, band.edge == 1 || band.edge == 3 ? static_cast<float>(height) : static_cast<float>(width),
            static_cast<float>(c.wrapJoinDistance), stableDepth
        );
      }
      if (b.toolSpan) {
        material.hazeProfile = b.hazeProfile;
        material.hazeBounds = b.hazeBounds;
        material.surfaceBackground = !b.hazeProfile.empty();
        if (b.backdrop.texture.valid()) {
          material.backdrop = b.backdrop;
          material.backdrop.opacity *= b.opacity;
        }
        appendScreenFrameTool(material.contour, band, *b.toolSpan,
            static_cast<float>(c.marginEdge) + b.toolDepth, b.toolIslandIndex);
      }
      material.contour.bands.push_back(std::move(band));
      if (b.hole.width > 0)
        material.exclusion = {
            static_cast<float>(b.hole.x), static_cast<float>(b.hole.y), static_cast<float>(b.hole.width),
            static_cast<float>(b.hole.height)
        };
    }
    if (screen_wrap::hasTopLeftJoin(material.contour)) {
      std::vector<BarConfig> visibleBars;
      for (const auto& [id, bar] : bars)
        if (bar.opacity > 0.001F) visibleBars.push_back(bar.config);
      const auto logo = screen_wrap::topLeftBrandingRect(visibleBars);
      screen_wrap::reserveIslandSpace(geometry, "top", logo.y + logo.height);
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
        strip.surface->setPrepareFrameCallback([this, i](bool, bool) {
          auto& s = strips[i];
          if (s.cornerLogo && s.cornerLogo->visible()) {
            const bool hadImage = s.cornerLogo->hasImage();
            const bool loaded = s.cornerLogo->setSourceFile(s.surface->renderTarget().renderer(),
                paths::assetPath("nythera-logo.svg").string(), static_cast<int>(std::ceil(s.cornerLogo->width())), true);
            if (!hadImage && loaded) {
              static constexpr Logger log("screen-wrap");
              log.info("loaded Nythera corner logo ({}px)", s.cornerLogo->width());
            }
          }
        });
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
    ScreenWrap* sibling, std::size_t priority, Wallpaper* wallpaper
)
    : m_shared(sibling ? sibling->m_shared : std::make_shared<Shared>(w, r, o, width, height, wallpaper)), m_id(priority) {
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
void ScreenWrap::updateWallpaperUnderlap(const CompositorPlatform& platform) {
  std::vector<BarConfig> configs;
  std::vector<int> requests;
  for (const auto& [id, bar] : m_shared->bars) {
    configs.push_back(bar.config);
    requests.push_back(bar.reservation);
  }
  const auto zones = screen_wrap::boundedReservations(
      screen_wrap::reservations(configs, requests), m_shared->width, m_shared->height);
  const bool underlap = platform.hasVisibleLeftUnderlap(
      m_shared->output, zones[3] + screen_wrap::leftBackingExtension, zones[1]);
  if (underlap) m_shared->backingRestoreTimer.stop();
  if (underlap == m_shared->leftWallpaperUnderlap) return;
  if (underlap) {
    m_shared->setLeftWallpaperUnderlap(true);
  } else if (!m_shared->backingRestoreTimer.active()) {
    // Umbriel's tiled IPC geometry is the destination, not the in-flight
    // presentation. Keep the backing out of the returning window's way while
    // its move settles. Reentry cancels this one-shot; no animation polling.
    m_shared->backingRestoreTimer.start(std::chrono::milliseconds(Style::animSlow), [shared = m_shared.get()] {
      shared->setLeftWallpaperUnderlap(false);
    });
  }
}
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

void ScreenWrap::setAttachedTool(
    std::optional<ScreenFrameSpan> span, float depth, float bufferDepth, int islandIndex, ScreenFrameBackdrop backdrop,
    std::string hazeProfile, std::array<float, 4> hazeBounds
) {
  auto& b = m_shared->bars.at(m_id);
  if (b.toolSpan == span
      && b.toolDepth == depth
      && b.toolBufferDepth == bufferDepth
      && b.toolIslandIndex == islandIndex
      && b.backdrop == backdrop
      && b.hazeProfile == hazeProfile
      && b.hazeBounds == hazeBounds)
    return;
  b.toolSpan = span; b.toolDepth = depth; b.toolBufferDepth = bufferDepth;
  b.toolIslandIndex = islandIndex;
  b.backdrop = backdrop;
  b.hazeProfile = std::move(hazeProfile);
  b.hazeBounds = hazeBounds;
  m_shared->refresh();
}
