#include "shell/bar/dynamic_island.h"

#include "config/config_service.h"
#include "core/deferred_call.h"
#include "dbus/mpris/mpris_art.h"
#include "dbus/mpris/mpris_service.h"
#include "pipewire/pipewire_service.h"
#include "pipewire/pipewire_spectrum.h"
#include "ui/visuals/audio_visualizer.h"
#include <numbers>
#include "render/core/renderer.h"
#include "render/scene/screen_frame_node.h"
#include "shell/bar/dynamic_island_style.h"
#include "shell/bar/island_activities.h"
#include "shell/bar/island_notification.h"
#include "shell/bar/screen_wrap.h"
#include "shell/panel/panel_manager.h"
#include "time/time_format.h"
#include "ui/builders.h"
#include "ui/palette.h"
#include "wayland/wayland_seat.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>

namespace {
  std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
  bool vertical(const BarConfig& c) { return c.position == "left" || c.position == "right"; }
  float flare(const BarConfig& c) {
    return std::max(24.0F, 2.0F * std::min(static_cast<float>(c.radius), static_cast<float>(c.thickness) / 2));
  }
} // namespace

LayerSurfaceConfig DynamicIsland::surfaceConfig(const BarConfig& c, int outputWidth, int outputHeight) {
  const bool side = vertical(c), bottom = c.position == "bottom", right = c.position == "right";
  const int gap = std::clamp(c.marginEdge, 0, std::min(outputWidth, outputHeight) / 4);
  // Only the narrow bar strip grows. The existing panel host owns the native tool's surface.
  const int strip = c.thickness; // contour flares live in the shared decorative buffers
  const int width = side ? std::min(strip, outputWidth - gap) : outputWidth;
  const int height = side ? outputHeight : std::min(strip, outputHeight - gap);
  return {
      .nameSpace = "noctalia-bar-dynamic-" + c.name,
      .layer = layerShellLayerFromConfig(c.layer),
      .anchor = side ? (right ? LayerShellAnchor::Right : LayerShellAnchor::Left)
                     : (bottom ? LayerShellAnchor::Bottom : LayerShellAnchor::Top),
      .width = static_cast<std::uint32_t>(std::max(1, width)),
      .height = static_cast<std::uint32_t>(std::max(1, height)),
      .exclusiveZone = c.screenWrap ? -1 : c.reserveSpace ? c.thickness : 0,
      .marginTop = !side && !bottom ? gap : 0,
      .marginRight = right ? gap : 0,
      .marginBottom = bottom ? gap : 0,
      .marginLeft = side && !right ? gap : 0,
      .defaultWidth = static_cast<std::uint32_t>(std::max(1, width)),
      .defaultHeight = static_cast<std::uint32_t>(std::max(1, height)),
      .prewarmBlur = c.compositorBlur
  };
}

DynamicIsland::DynamicIsland(
    const BarServices& services, const BarConfig& config, LayerSurface& surface, AnimationManager& animations,
    ScreenWrap* wrap, wl_output* output, int width, int height
)
    : m_services(services), m_config(config), m_surface(surface), m_animations(animations), m_wrap(wrap),
      m_output(output), m_outputWidth(width), m_outputHeight(height), m_vertical(vertical(config)),
      m_reverse(config.position == "bottom" || config.position == "right") {
  const auto& privacy = m_services.config.config().shell.privacy;
  m_privacyFilters[0].update("shell.privacy.mic_filter_regex", privacy.micFilterRegex);
  m_privacyFilters[1].update("shell.privacy.cam_filter_regex", privacy.camFilterRegex);
  m_privacyFilters[2].update("shell.privacy.screen_filter_regex", privacy.screenFilterRegex);
  m_paletteConnection = paletteChanged().connect([this] { m_surface.requestUpdate(); });
  if (m_services.activities) {
    m_activityConnection = m_services.activities->changed.connect([this] { syncActivities(); });
    syncActivities();
  }
}
DynamicIsland::~DynamicIsland() {
  m_alive.reset();
  m_animations.cancel(m_contentAnimation);
  m_animations.cancel(m_resizeAnimation);
  syncSpectrum(false);
  while (const auto* entry = m_noticeQueue.front()) {
    const auto n = entry->notice;
    if (m_services.notifications && n.timeout != 0)
      m_services.notifications->resumeExpiry(n.id, std::max(1, n.timeout));
    m_noticeQueue.erase(n.id);
  }
  if (auto* panels = PanelManager::current(); panels && panels->isDynamicIslandPanel(m_output, m_config.name))
    panels->closePanel(false);
}

std::string_view DynamicIsland::nativeToolContext(IslandEvent kind) {
  switch (kind) {
  case IslandEvent::Media:
    return "media";
  case IslandEvent::Volume:
    return "audio";
  case IslandEvent::Brightness:
    return "monitor";
  case IslandEvent::Privacy:
    return "audio";
  case IslandEvent::Device:
    return "home";
  case IslandEvent::Notification:
  case IslandEvent::Call:
    return "notifications";
  default:
    return "home";
  }
}
float DynamicIsland::compactExtent(IslandEvent) const { return m_compactExtent; }

void DynamicIsland::build() {
  m_root = ui::node({});
  m_root->setAnimationManager(&m_animations);
  auto clip = ui::node({});
  clip->setClipChildren(true);
  clip->setHitTestVisible(false);
  m_frameClip = m_root->addChild(std::move(clip));
  m_background = static_cast<Box*>(m_frameClip->addChild(ui::box()));
  if (m_wrap) {
    auto frame = std::make_unique<ScreenFrameNode>();
    frame->setHitTestVisible(false);
    m_frame = static_cast<ScreenFrameNode*>(m_frameClip->addChild(std::move(frame)));
  }
  auto body = ui::inputArea({});
  body->setClipChildren(true);
  body->setOnClick([this](const auto&) { openNativeTool(); });
  m_body = static_cast<InputArea*>(m_root->addChild(std::move(body)));
  m_content = m_body->addChild(ui::node({}));
  m_indicatorContent = m_body->addChild(ui::node({}));
  m_activityLabel = static_cast<Label*>(m_indicatorContent->addChild(ui::label({})));
  m_measureLabel = static_cast<Label*>(m_root->addChild(ui::label({})));
  m_measureLabel->setVisible(false);
  m_measureLabel->setHitTestVisible(false);
  m_content->addChild(ui::glyph({.out = &m_glyph, .glyph = "clock"}));
  m_content->addChild(ui::label({.out = &m_title}));
  m_content->addChild(ui::image({.out = &m_art, .fit = ImageFit::Cover}));
  m_visualizer = static_cast<AudioVisualizer*>(m_content->addChild(std::make_unique<AudioVisualizer>()));
  m_visualizer->setHitTestVisible(false);
  m_progress = static_cast<ProgressBar*>(m_content->addChild(std::make_unique<ProgressBar>()));
  for (auto& action : m_actions)
    action = static_cast<Button*>(m_content->addChild(ui::button({.variant = ButtonVariant::Ghost})));
  m_dispatcher.setSceneRoot(m_root.get());
  m_surface.setSceneRoot(m_root.get());
  syncClockTimer();
}

bool DynamicIsland::feedback(const OsdContent& content) {
  if (!m_visible)
    return false;
  IslandEvent kind;
  if ((content.kind == OsdKind::Volume || content.kind == OsdKind::Microphone) && m_config.dynamicVolume)
    kind = IslandEvent::Volume;
  else if (content.kind == OsdKind::Brightness && m_config.dynamicBrightness)
    kind = IslandEvent::Brightness;
  else if (content.kind == OsdKind::Media && m_config.dynamicMedia)
    kind = IslandEvent::Media;
  else if (content.kind == OsdKind::Privacy && m_config.dynamicPrivacy)
    kind = IslandEvent::Privacy;
  else if ((content.kind == OsdKind::Wifi || content.kind == OsdKind::Bluetooth
            || content.kind == OsdKind::PowerProfile) && m_config.dynamicDevices)
    kind = IslandEvent::Device;
  else
    return false;
  m_feedback[static_cast<unsigned>(kind)] = content;
  m_state.push(kind, nowMs(), m_config.dynamicDwellMs);
  m_surface.requestUpdate();
  return true;
}

void DynamicIsland::syncNoticeQueue() {
  const auto stamp = nowMs();
  const bool displayed = m_state.resolve(stamp) == IslandEvent::Notification;
  if (auto expired = m_noticeQueue.advance(stamp, displayed, m_hovered || m_nativeOwned || !m_visible)) {
    if (m_services.notifications && expired->timeout != 0)
      m_services.notifications->resumeExpiry(expired->id, 1);
  }
  // DND discards queued Respect notices, but never bypass notices. Release
  // the manager expiry ownership instead of leaving paused notifications behind.
  while (const auto* entry = m_noticeQueue.front()) {
    if (!m_services.notifications || !m_services.notifications->doNotDisturb()
        || entry->notice.dndPolicy != NotificationDndPolicy::Respect) break;
    const auto n = entry->notice;
    if (n.timeout != 0) m_services.notifications->resumeExpiry(n.id, std::max(1, n.timeout));
    m_noticeQueue.erase(n.id);
  }
  if (const auto* entry = m_noticeQueue.front()) {
    m_notices[0] = entry->notice;
    m_state.hold(IslandEvent::Notification);
  } else {
    m_notices[0] = {};
    m_state.clear(IslandEvent::Notification);
  }
}

void DynamicIsland::syncSpectrum(bool enabled) {
  if (!m_services.audioSpectrum) return;
  if (enabled && !m_spectrumListener)
    m_spectrumListener = m_services.audioSpectrum->addChangeListener(16, [this] { m_surface.requestUpdate(); });
  else if (!enabled && m_spectrumListener) {
    m_services.audioSpectrum->removeChangeListener(m_spectrumListener);
    m_spectrumListener = 0;
    m_spectrumTick = 0;
  }
}

void DynamicIsland::syncIndicators(Renderer& renderer) {
  std::string text;
  auto append = [&](std::string value) { if (!text.empty()) text += m_vertical ? "\n" : " · "; text += value; };
  if (m_config.dynamicActivityIndicators) {
    if (m_services.activities && m_config.dynamicTimers && m_services.activities->timerTarget == m_output
        && m_services.activities->timerEnd && m_event != IslandEvent::Timer)
      append(m_services.activities->countdown());
    if (m_services.audio) {
      std::array<bool, 3> active{};
      for (const auto& capture : m_services.audio->privacyState().captures) {
        const auto i = static_cast<unsigned>(capture.kind);
        if (i < active.size() && !m_privacyFilters[i].matches(capture.appName)) active[i] = true;
      }
      if (active[0]) append("MIC");
      if (active[1]) append("CAM");
      if (active[2]) append("SHARE");
    }
  }
  m_activityLabel->setText(text);
  m_activityLabel->setFontFamily(m_config.fontFamily.value_or(m_services.config.config().shell.fontFamily));
  m_activityLabel->setFontSize(std::min(11 * m_config.scale, std::max(8.0F, static_cast<float>(m_config.thickness) * 0.3F)));
  m_activityLabel->setMaxLines(m_vertical ? 4 : 1);
  m_activityLabel->setMaxWidth(m_vertical ? std::max(1.0F, m_width - 12) : 200 * m_config.scale);
  m_activityLabel->setColor(colorSpecFromRole(ColorRole::Primary));
  m_activityLabel->measure(renderer);
  m_indicatorContent->setVisible(!text.empty());
  m_indicatorContent->setSize(m_width, m_height);
  m_indicatorContent->setOpacity(1 - m_nativeProgress);
  m_indicatorExtent = text.empty() ? 0 : (m_vertical ? m_activityLabel->height() : m_activityLabel->width()) + 12;
}

void DynamicIsland::resolve() {
  syncNoticeQueue();
  if (m_services.notifications && m_services.notifications->doNotDisturb())
    for (auto kind : {IslandEvent::Notification, IslandEvent::Call})
      if (m_notices[kind == IslandEvent::Call ? 1 : 0].dndPolicy == NotificationDndPolicy::Respect)
        m_state.clear(kind);
  auto kind = m_state.resolve(nowMs(), m_nativeOwned ? m_event : IslandEvent::Resting);
  if (kind == IslandEvent::Media && (!m_services.mpris || !m_services.mpris->activePlayer()))
    kind = IslandEvent::Resting;
  if (kind != m_event || !m_initialized) {
    m_event = kind;
    changeContent(kind);
  }
  const float extent = compactExtent(kind);
  const float native = m_nativeProgress > 0 ? extent + (m_nativeExtent - extent) * m_nativeProgress : extent;
  const float width = m_vertical ? static_cast<float>(m_config.thickness)
                                 : std::min(native, static_cast<float>(m_surface.width()) - flare(m_config) * 2);
  const float height = m_vertical ? std::min(native, static_cast<float>(m_surface.height()) - flare(m_config) * 2)
                                  : static_cast<float>(m_config.thickness);
  if (!m_initialized) {
    m_width = width;
    m_height = height;
    m_initialized = true;
  }
  if (width != m_targetWidth || height != m_targetHeight) {
    m_animations.cancel(m_resizeAnimation);
    m_targetWidth = width;
    m_targetHeight = height;
    if (m_nativeProgress > 0) {
      m_width = width;
      m_height = height;
    } else {
      const float fromW = m_width, fromH = m_height;
      m_resizeAnimation = m_animations.animate(
          0, 1, 300, Easing::EaseOutCubic,
          [this, fromW, fromH, width, height](float t) {
            m_width = fromW + (width - fromW) * t;
            m_height = fromH + (height - fromH) * t;
            m_surface.requestLayout();
          },
          {}, m_root.get()
      );
    }
  }
  m_content->setOpacity(m_contentOpacity * (1.0F - m_nativeProgress));
  m_indicatorContent->setOpacity(1.0F - m_nativeProgress);
  m_expiryTimer.stop();
  const auto remaining = m_state.remaining(kind, nowMs());
  const auto* notice = m_noticeQueue.front();
  const auto timeout = kind == IslandEvent::Notification && notice
      ? (m_nativeOwned || m_hovered ? 250 : notice->remaining) : remaining;
  if (kind != IslandEvent::Resting && timeout > 0)
    m_expiryTimer.start(std::chrono::milliseconds(timeout + 1), [this] { m_surface.requestUpdate(); });
}
void DynamicIsland::changeContent(IslandEvent kind) {
  if (!m_initialized) m_displayEvent = kind;
  // layout() stages the compact content fade; native panels keep their own reveal.
}
void DynamicIsland::prepare(Renderer& renderer, bool update) {
  if (!m_root)
    build();
  if (update || !m_initialized)
    resolve();
  geometry();
  layout(renderer);
}
void DynamicIsland::geometry() {
  const float sw = static_cast<float>(m_surface.width()), sh = static_cast<float>(m_surface.height());
  const float gap = static_cast<float>(std::clamp(m_config.marginEdge, 0, std::min(m_outputWidth, m_outputHeight) / 4));
  m_originX = m_vertical ? (m_reverse ? static_cast<float>(m_outputWidth) - gap - sw : gap)
                         : std::floor((static_cast<float>(m_outputWidth) - sw) / 2);
  m_originY = m_vertical ? std::floor((static_cast<float>(m_outputHeight) - sh) / 2)
                         : (m_reverse ? static_cast<float>(m_outputHeight) - gap - sh : gap);
  m_root->setSize(sw, sh);
  const float x = m_vertical ? (m_reverse ? sw - m_width : 0) : (sw - m_width) / 2;
  const float y = m_vertical ? (sh - m_height) / 2 : (m_reverse ? sh - m_height : 0);
  m_body->setPosition(x, y);
  m_body->setSize(m_width, m_height);
  m_content->setSize(m_width, m_height);
  const float extension =
      dynamic_island_style::joinExtension(m_nativeProgress, static_cast<float>(m_config.radius), m_config.borderWidth);
  const float panelOpacity =
      detachedPanelBackgroundOpacityForTransparencyMode(m_services.config.config().shell.panel.transparencyMode);
  const float opacity = std::lerp(m_config.backgroundOpacity, panelOpacity, m_nativeProgress);
  // Each surface paints only its own side of the join: no doubled translucent
  // fill or blur where the native tool touches the neck.
  const bool joined = m_nativeProgress > 0;
  const float clipX = joined && m_vertical && m_reverse ? x : 0;
  const float clipY = joined && !m_vertical && m_reverse ? y : 0;
  const float clipW = joined && m_vertical ? m_width : sw;
  const float clipH = joined && !m_vertical ? m_height : sh;
  m_frameClip->setPosition(clipX, clipY);
  m_frameClip->setSize(clipW, clipH);
  m_background->setPosition(
      x - clipX - (m_vertical && m_reverse ? extension : 0), y - clipY - (!m_vertical && m_reverse ? extension : 0)
  );
  m_background->setSize(m_width + (m_vertical ? extension : 0), m_height + (m_vertical ? 0 : extension));
  // While expanded the native host paints the neck and tool as one backdrop.
  m_background->setVisible(!joined);
  m_background->setFill(colorForRole(ColorRole::Surface, m_wrap ? 0.0F : opacity));
  m_background->setBorder(resolveColorSpec(m_config.border), m_wrap ? 0.0F : m_config.borderWidth);
  m_background->setRadius(static_cast<float>(m_config.radius));
  if (m_wrap) {
    const float begin = m_vertical ? m_originY + y : m_originX + x;
    m_wrap->setIslands(
        {ScreenFrameSpan{}, ScreenFrameSpan{begin, begin + (m_vertical ? m_height : m_width)}, ScreenFrameSpan{}},
        joined ? m_nativeDepth : (m_vertical ? m_width : m_height)
    );
    m_wrap->setForegroundHole({static_cast<int>(m_nativeBackdrop[0]), static_cast<int>(m_nativeBackdrop[1]),
        static_cast<int>(m_nativeBackdrop[2]), static_cast<int>(m_nativeBackdrop[3])});
    m_frame->setVisible(false); // the display-owned contour paints every bar contribution once
    m_frame->setPosition(-clipX, -clipY);
    m_frame->setSize(sw, sh);
    m_frame->setStyle(
        {m_wrap->contour(), colorForRole(ColorRole::Surface, opacity), resolveColorSpec(m_config.border),
         m_config.borderWidth, m_originX, m_originY, m_nativeBackdrop}
    );
  }
  const float radius = static_cast<float>(m_config.radius);
  const auto nativeRadii = dynamic_island_style::panelRadii(m_config.position, radius);
  const Radii radii = joined
      ? Radii{radius - nativeRadii.tl, radius - nativeRadii.tr, radius - nativeRadii.br, radius - nativeRadii.bl}
      : Radii{radius};
  const auto neckRegion = Surface::tessellateShape(
      static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), static_cast<int>(std::ceil(m_width)),
      static_cast<int>(std::ceil(m_height)), CornerShapes{}, RectInsets{}, radii
  );
  m_surface.setInputRegion(m_visible ? neckRegion : std::vector<InputRect>{});
  if (m_wrap || !m_config.compositorBlur || !m_visible)
    m_surface.clearBlurRegion();
  else {
    auto blur = neckRegion;
    if (m_wrap)
      blur = m_wrap->blurRegion(
          {static_cast<int>(m_originX + clipX), static_cast<int>(m_originY + clipY), static_cast<int>(clipW),
           static_cast<int>(clipH)}
      );
    if (joined && m_nativeBackdrop[2] > 0) {
      const int left = static_cast<int>(m_nativeBackdrop[0] - m_originX);
      const int top = static_cast<int>(m_nativeBackdrop[1] - m_originY);
      const int right = left + static_cast<int>(m_nativeBackdrop[2]);
      const int bottom = top + static_cast<int>(m_nativeBackdrop[3]);
      std::vector<InputRect> outside;
      for (const auto r : blur) {
        const int x0 = std::max(r.x, left), y0 = std::max(r.y, top);
        const int x1 = std::min(r.x + r.width, right), y1 = std::min(r.y + r.height, bottom);
        if (x1 <= x0 || y1 <= y0) {
          outside.push_back(r);
          continue;
        }
        if (r.y < y0)
          outside.push_back({r.x, r.y, r.width, y0 - r.y});
        if (y1 < r.y + r.height)
          outside.push_back({r.x, y1, r.width, r.y + r.height - y1});
        if (r.x < x0)
          outside.push_back({r.x, y0, x0 - r.x, y1 - y0});
        if (x1 < r.x + r.width)
          outside.push_back({x1, y0, r.x + r.width - x1, y1 - y0});
      }
      blur = std::move(outside);
    }
    m_surface.setBlurRegion(blur);
  }
}

std::string DynamicIsland::contentTitle(IslandEvent kind) const {
  const auto player = m_services.mpris ? m_services.mpris->activePlayer() : std::nullopt;
  const bool media = kind == IslandEvent::Media && player.has_value();
  const auto& feedback = m_feedback[static_cast<unsigned>(kind)];
  std::string title = kind == IslandEvent::Resting          ? formatLocalTime(m_config.dynamicClockFormat.c_str())
      : media                                               ? player->title
      : kind == IslandEvent::Notification                   ? m_notices[0].summary
      : kind == IslandEvent::Call                           ? m_notices[1].summary
      : kind == IslandEvent::Timer && m_services.activities ? m_services.activities->countdown()
      : kind == IslandEvent::TimerFinished                  ? "Timer finished"
      : kind == IslandEvent::Alarm                          ? "Alarm"
                                                            : feedback.value;
  if (m_vertical && kind == IslandEvent::Resting)
    std::ranges::replace(title, ':', '\n');
  if (kind == IslandEvent::Call && m_notices[1].category == "call.ongoing")
    title = "In call · " + title;
  return title;
}

void DynamicIsland::layout(Renderer& renderer) {
  syncIndicators(renderer);
  const auto wanted = m_event;
  const auto desiredTitle = contentTitle(wanted);
  const bool descriptive = wanted == IslandEvent::Media || wanted == IslandEvent::Notification || wanted == IslandEvent::Call;
  std::string signature = std::to_string(static_cast<int>(wanted)) + (descriptive ? desiredTitle : "");
  if (wanted == IslandEvent::Media && m_services.mpris)
    if (const auto player = m_services.mpris->activePlayer()) signature += mpris::effectiveArtUrl(*player);
  if (m_contentSignature.empty() || m_nativeProgress > 0) {
    m_contentSignature = signature;
    m_displayEvent = wanted;
  } else if (signature != m_contentSignature && !m_fadingOut) {
    m_fadingOut = true;
    m_animations.cancel(m_contentAnimation);
    const float from = m_contentOpacity;
    m_contentAnimation = m_animations.animate(0, 1, 120, Easing::EaseOutCubic,
      [this, from](float t) { m_contentOpacity = from * (1-t); m_surface.requestLayout(); },
      [this] {
        m_fadingOut = false;
        m_displayEvent = m_event;
        m_contentSignature.clear();
        m_contentAnimation = m_animations.animate(0, 1, 300, Easing::EaseOutCubic,
          [this](float t) { m_contentOpacity = t; m_surface.requestLayout(); }, {}, m_root.get());
        m_surface.requestUpdate();
      }, m_root.get());
  }
  m_content->setOpacity(m_contentOpacity * (1 - m_nativeProgress));
  if (m_fadingOut) return;

  const auto kind = m_displayEvent;
  const auto player = m_services.mpris ? m_services.mpris->activePlayer() : std::nullopt;
  const bool media = kind == IslandEvent::Media && player.has_value();
  const bool level = kind == IslandEvent::Volume || kind == IslandEvent::Brightness;
  const auto& feedback = m_feedback[static_cast<unsigned>(kind)];
  std::string title = contentTitle(kind);
  const float scale = m_config.scale;
  const float fontScale = scale * m_config.fontScale;
  const float iconSize = std::min(20 * scale, std::max(8.0F, static_cast<float>(m_config.thickness) - 12));
  const float artSize = std::min(28 * scale, std::max(8.0F, static_cast<float>(m_config.thickness) - 12));
  const float actionSize = std::min(30 * scale, std::max(16.0F, static_cast<float>(m_config.thickness) - 8));
  m_title->setText(title);
  m_title->setFontFamily(m_config.fontFamily.value_or(m_services.config.config().shell.fontFamily));
  m_title->setFontSize(16 * fontScale);
  m_title->setFontWeight(static_cast<FontWeight>(m_config.fontWeight));
  m_title->setColor(colorSpecFromRole(ColorRole::OnSurface));
  m_title->setMaxLines(m_vertical ? 3 : 1);
  m_glyph->setGlyph(
      media                                                                    ? "disc"
          : kind == IslandEvent::Resting                                       ? "clock"
          : kind == IslandEvent::Notification                                  ? "bell"
          : kind == IslandEvent::Call                                          ? "phone"
          : kind == IslandEvent::Alarm                                         ? "alarm"
          : (kind == IslandEvent::Timer || kind == IslandEvent::TimerFinished) ? "hourglass"
                                                                               : feedback.icon
  );
  m_glyph->setGlyphSize(iconSize);
  m_glyph->setColor(colorSpecFromRole(ColorRole::Primary));
  m_glyph->measure(renderer);
  m_glyph->setVisible(kind != IslandEvent::Resting);
  m_art->setVisible(false);
  if (media) {
    const auto path = mpris::resolveArtworkSource(
        m_services.httpClient, m_pendingArt, mpris::effectiveArtUrl(*player), [this] { m_surface.requestUpdate(); },
        m_alive
    );
    if (!path.empty() && path != m_artPath) {
      m_artPath = path;
      m_art->setSourceFile(renderer, path, std::max(64, static_cast<int>(std::ceil(artSize))));
      m_animations.cancelForOwner(m_art);
      m_art->setOpacity(0);
      m_animations.animate(0, 1, 180, Easing::EaseOutCubic,
          [this](float t) { m_art->setOpacity(t); m_surface.requestLayout(); }, {}, m_art);
    }
    if (!path.empty()) {
      m_art->setVisible(true);
      m_art->setSize(artSize, artSize);
      m_art->setRadius(6 * scale);
      m_glyph->setVisible(false);
    }
  }
  for (auto* action : m_actions)
    action->setVisible(false);
  std::size_t count = 0;
  auto action = [&](std::string glyph, std::function<void()> callback, std::optional<ColorSpec> color = std::nullopt) {
    if (count >= m_actions.size())
      return;
    auto* button = m_actions[count++];
    button->setVisible(true);
    button->setGlyph(glyph);
    button->setOnClick(std::move(callback));
    button->setControlHeight(actionSize);
    button->setGlyphSize(std::min(18 * scale, actionSize - 8));
    auto colors = Button::defaultPalette(ButtonVariant::Ghost);
    if (color) {
      colors.normal.label = *color;
      colors.hover.label = *color;
      colors.pressed.label = *color;
    }
    button->setCustomPalette(colors);
    button->layout(renderer);
  };
  if (kind == IslandEvent::Call && m_services.notifications) {
    const auto n = m_notices[1];
    for (bool accept : {true, false})
      for (std::size_t i = 0; i + 1 < n.actions.size(); i += 2) {
        auto text = n.actions[i] + " " + n.actions[i + 1];
        std::ranges::transform(text, text.begin(), [](unsigned char c) { return std::tolower(c); });
        const bool match = accept
            ? (text.find("accept") != std::string::npos || text.find("answer") != std::string::npos)
            : (text.find("decline") != std::string::npos
               || text.find("reject") != std::string::npos
               || text.find("hang") != std::string::npos);
        if (match) {
          action(
              accept ? "phone" : "phone-off",
              [this, id = n.id, key = n.actions[i]] { (void)m_services.notifications->invokeAction(id, key); },
              colorSpecFromConfigString(accept ? "#4caf78" : "#ef5350")
          );
          break;
        }
      }
    action("x", [this] { dismissEvent(); });
  } else if (kind == IslandEvent::Timer || kind == IslandEvent::TimerFinished)
    action("player-stop", [this] { dismissEvent(); });
  else if (kind == IslandEvent::Alarm) {
    action("alarm-snooze", [this] {
      if (m_services.activities)
        m_services.activities->snooze(m_config.dynamicSnoozeSeconds);
    });
    action("x", [this] { dismissEvent(); });
  }
  float actionSpace = 0;
  for (std::size_t i = 0; i < count; ++i) {
    auto* b = m_actions[i];
    actionSpace += b->width() + 3;
    b->setPosition(
        m_vertical ? (m_width - b->width()) / 2 : m_width - actionSpace - 6,
        m_vertical ? m_height - static_cast<float>(count - i) * (actionSize + 3) - 6 : (m_height - b->height()) / 2
    );
  }
  const bool rotated = m_vertical && kind != IslandEvent::Resting && kind != IslandEvent::Timer
      && kind != IslandEvent::TimerFinished && !level;
  const float titleLimit = std::max(1.0F, (m_vertical ? m_height : m_width)
      - (kind == IslandEvent::Resting ? 24 : artSize + 32) - actionSpace - m_indicatorExtent);
  m_title->setMaxWidth(rotated || !m_vertical ? titleLimit : std::max(1.0F, m_width - 12));
  m_title->setMaxLines(m_vertical && !rotated ? 3 : 1);
  m_title->setTransformOrigin(0, 0);
  m_title->setRotation(rotated ? -std::numbers::pi_v<float> / 2 : 0);
  m_title->measure(renderer);
  if (m_vertical) {
    m_glyph->setPosition((m_width - m_glyph->width()) / 2, 10);
    m_art->setPosition((m_width - artSize) / 2, 10);
    m_title->setPosition(rotated ? (m_width - m_title->height()) / 2 : (m_width - m_title->width()) / 2,
        rotated ? m_height - actionSpace - m_indicatorExtent - 10
                : kind == IslandEvent::Resting ? (m_height - m_indicatorExtent - m_title->height()) / 2 : artSize + 16);
  } else {
    m_glyph->setPosition(14, (m_height - m_glyph->height()) / 2 - (level ? 3 : 0));
    m_art->setPosition(12, (m_height - artSize) / 2);
    m_title->setPosition(kind == IslandEvent::Resting ? (m_width - m_indicatorExtent - m_title->width()) / 2 : artSize + 22,
        (m_height - m_title->height()) / 2 - (level ? 4 : 0));
  }
  m_activityLabel->setPosition(m_vertical ? (m_width - m_activityLabel->width()) / 2
                                        : m_width - actionSpace - m_indicatorExtent + 4,
      m_vertical ? m_height - actionSpace - m_indicatorExtent + 4 : (m_height - m_activityLabel->height()) / 2);
  // Measure the full untruncated title, then bound its desired main-axis extent.
  m_measureLabel->setText(desiredTitle);
  m_measureLabel->setFontFamily(m_config.fontFamily.value_or(m_services.config.config().shell.fontFamily));
  m_measureLabel->setFontWeight(static_cast<FontWeight>(m_config.fontWeight));
  m_measureLabel->setFontSize(16 * fontScale);
  m_measureLabel->setMaxLines(m_vertical && !rotated ? 3 : 1);
  m_measureLabel->setMaxWidth(m_vertical && !rotated ? std::max(1.0F, m_width - 12) : 10000);
  m_measureLabel->measure(renderer);
  const float textExtent = m_vertical && !rotated ? m_measureLabel->height() : m_measureLabel->width();
  const float measured = textExtent + (kind == IslandEvent::Resting ? 24 : artSize + 32) + actionSpace + m_indicatorExtent;
  const float available = std::max(1.0F, (m_vertical ? static_cast<float>(m_surface.height()) : static_cast<float>(m_surface.width())) - flare(m_config) * 2);
  const float target = std::min(available, std::clamp(measured, 76 * scale, 420 * scale));
  if (std::abs(target - m_compactExtent) > 0.5F) { m_compactExtent = target; m_surface.requestUpdate(); }
  const bool spectrum = m_config.dynamicVisualizer && media && player->playbackStatus == "Playing"
      && m_visible && m_nativeProgress == 0;
  syncSpectrum(spectrum);
  m_visualizer->setVisible(spectrum);
  if (spectrum && m_services.audioSpectrum) {
    m_visualizer->setValues(m_services.audioSpectrum->values(m_spectrumListener));
    const auto stamp = nowMs();
    m_visualizer->advance(m_spectrumTick ? static_cast<float>(stamp - m_spectrumTick) : 16, !m_services.audioSpectrum->idle());
    m_spectrumTick = stamp;
    m_visualizer->setGradient(colorSpecFromRole(ColorRole::Primary), colorSpecFromRole(ColorRole::Primary));
    m_visualizer->setPosition(8, m_height - 8);
    m_visualizer->setSize(std::max(1.0F, m_width - 16), 5);
  }
  m_progress->setVisible(level);
  m_progress->setPosition(m_vertical ? 6 : 16, m_height - 9);
  m_progress->setSize(std::max(1.0F, m_width - (m_vertical ? 12 : 32)), 3);
  m_progress->setFill(colorSpecFromRole(ColorRole::Primary));
  m_progress->setTrack(colorSpecFromRole(ColorRole::SurfaceContainerHighest));
  m_progress->setProgress(std::clamp(feedback.progress, 0.0F, 1.0F));
}

void DynamicIsland::syncClockTimer() {
  const auto epoch =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count();
  const auto interval = formatShowsSeconds(m_config.dynamicClockFormat) ? 1000 : 60000;
  m_clockTimer.start(std::chrono::milliseconds(interval - epoch % interval), [this] {
    m_surface.requestUpdate();
    syncClockTimer();
  });
}
void DynamicIsland::openNativeTool(bool hover) {
  auto* panels = PanelManager::current();
  if (!panels || !m_visible || (hover && m_hoverBlocked))
    return;
  // The reopen guard prevents hover loops after dismissal, never explicit clicks.
  if (!hover) { m_hoverBlocked = false; m_pinned = true; }
  if (panels->isDynamicIslandPanel(m_output, m_config.name)) {
    if (!hover) panels->promoteIslandInteraction();
    return;
  }
  auto context = nativeToolContext(m_event);
  if (m_event == IslandEvent::Device) {
    const auto kind = m_feedback[static_cast<unsigned>(m_event)].kind;
    context = kind == OsdKind::Wifi ? "network" : kind == OsdKind::Bluetooth ? "bluetooth" : "power";
  }
  panels->openPanel(
      "control-center",
      {.output = m_output,
       .anchorX = static_cast<float>(m_outputWidth) / 2,
       .anchorY = static_cast<float>(m_outputHeight) / 2,
       .hasExplicitAnchor = true,
       .hasAnchorPosition = true,
       .context = context,
       .sourceBarName = m_config.name,
       .fromDynamicIsland = true,
       .islandCompactExtent = compactExtent(m_event),
       .islandFrameProvider = m_wrap ? std::function<ScreenFrameStyle()>{[weak = std::weak_ptr<void>(m_alive), wrap = m_wrap] {
         return weak.expired() ? ScreenFrameStyle{} : wrap->style();
       }} : std::function<ScreenFrameStyle()>{},
       .islandHoverOpen = hover}
  );
  m_nativeOwned = panels->isDynamicIslandPanel(m_output, m_config.name);
}
void DynamicIsland::toggleExpanded() {
  auto* panels = PanelManager::current();
  if (panels && panels->isDynamicIslandPanel(m_output, m_config.name)) {
    m_hoverBlocked = true;
    panels->closePanel();
  } else {
    m_hoverBlocked = false;
    openNativeTool();
  }
}
void DynamicIsland::setNativePanelGeometry(std::optional<AttachedPanelGeometry> geometry) {
  if (geometry && geometry->islandExtent <= 0.0F)
    geometry.reset();
  m_nativeProgress = geometry ? geometry->revealProgress : 0;
  m_nativeExtent = geometry ? geometry->islandExtent : 0;
  m_nativeDepth = geometry ? geometry->islandDepth : 0;
  m_nativeBackdrop = geometry ? geometry->islandSurface : std::array<float, 4>{};
  if (!geometry && m_nativeOwned) {
    m_nativeOwned = false;
    m_pinned = false;
    m_hoverBlocked = m_hovered;
  }
  m_surface.requestUpdate();
}
void DynamicIsland::watchNativeLeave() {
  m_hoverTimer.start(std::chrono::milliseconds(250), [this] {
    auto* panels = PanelManager::current();
    if (!panels || !panels->isDynamicIslandPanel(m_output, m_config.name))
      return;
    if (m_pinned) return;
    if (m_hovered || panels->pointerHoldsPanel()) {
      watchNativeLeave();
      return;
    }
    panels->closePanel();
  });
}
bool DynamicIsland::overAction(float x, float y) const {
  if (!m_body || m_nativeOwned) return false;
  for (const auto* action : m_actions)
    if (action && action->visible() && x >= m_body->x() + action->x()
        && x < m_body->x() + action->x() + action->width()
        && y >= m_body->y() + action->y() && y < m_body->y() + action->y() + action->height())
      return true;
  return false;
}

bool DynamicIsland::onPointerEvent(const PointerEvent& e) {
  if (!m_visible)
    return false;
  switch (e.type) {
  case PointerEvent::Type::Enter:
    m_hovered = true;
    m_pointerOnAction = overAction(static_cast<float>(e.sx), static_cast<float>(e.sy));
    m_hoverTimer.stop();
    m_dispatcher.pointerEnter(static_cast<float>(e.sx), static_cast<float>(e.sy), e.serial);
    if (m_config.dynamicHoverExpand)
      m_hoverEntryTimer.start(std::chrono::milliseconds(180), [this] {
        if (m_hovered && !m_pointerOnAction) openNativeTool(true);
      });
    return true;
  case PointerEvent::Type::Leave:
    m_hoverEntryTimer.stop();
    m_hovered = false;
    m_hoverBlocked = false;
    m_dispatcher.pointerLeave();
    watchNativeLeave();
    return true;
  case PointerEvent::Type::Motion: {
    const bool previous = m_pointerOnAction;
    m_pointerOnAction = overAction(static_cast<float>(e.sx), static_cast<float>(e.sy));
    if (m_pointerOnAction) m_hoverEntryTimer.stop();
    else if (previous && m_config.dynamicHoverExpand)
      m_hoverEntryTimer.start(std::chrono::milliseconds(180), [this] {
        if (m_hovered && !m_pointerOnAction) openNativeTool(true);
      });
    m_dispatcher.pointerMotion(static_cast<float>(e.sx), static_cast<float>(e.sy), e.serial);
    return true;
  }
  case PointerEvent::Type::Button:
    return m_dispatcher.pointerButton(
        static_cast<float>(e.sx), static_cast<float>(e.sy), e.button, e.pressed, e.serial, e.time, e.touch
    );
  case PointerEvent::Type::Axis:
    return m_dispatcher.pointerAxis(
        static_cast<float>(e.sx), static_cast<float>(e.sy), e.axis, e.axisSource, e.axisValue, e.axisDiscrete,
        e.axisValue120, e.axisLines, e.axisGestureSerial
    );
  }
  return false;
}
void DynamicIsland::setVisible(bool visible) {
  if (m_visible == visible)
    return;
  m_visible = visible;
  if (m_root)
    m_root->setOpacity(visible ? 1.0F : 0.0F);
  if (!visible) {
    m_dispatcher.pointerLeave();
    m_hovered = false;
    m_hoverTimer.stop();
    m_hoverEntryTimer.stop();
    syncSpectrum(false);
    if (auto* panels = PanelManager::current(); panels && panels->isDynamicIslandPanel(m_output, m_config.name))
      panels->closePanel(false);
  }
  m_surface.requestUpdate();
}
void DynamicIsland::dismissEvent() {
  if (m_displayEvent == IslandEvent::Call && m_services.notifications)
    (void)m_services.notifications->close(m_notices[1].id, CloseReason::Dismissed);
  else if (m_services.activities) {
    if (m_displayEvent == IslandEvent::Alarm)
      m_services.activities->dismissAlarm();
    else
      m_services.activities->cancelTimer();
  }
}

bool DynamicIsland::notification(const Notification& n, NotificationEvent event) {
  const bool call = isIslandCall(n);
  if (event == NotificationEvent::Closed) m_noticeQueue.erase(n.id);
  const auto kind = call ? IslandEvent::Call : IslandEvent::Notification;
  auto& stored = m_notices[call ? 1 : 0];
  if (event == NotificationEvent::Closed) {
    for (auto k : {IslandEvent::Notification, IslandEvent::Call}) {
      auto& old = m_notices[k == IslandEvent::Call ? 1 : 0];
      if (old.id == n.id) {
        old = {};
        m_state.clear(k);
      }
    }
    m_surface.requestUpdate();
    return false;
  }
  if (!m_visible || (call ? !m_config.dynamicCalls : !m_config.dynamicNotifications))
    return false;
  if (!call) {
    if (event == NotificationEvent::Updated && !m_noticeQueue.contains(n.id)) return false;
    if (!m_noticeQueue.push(n, m_config.dynamicDwellMs)) return false;
    if (m_services.notifications && n.timeout != 0) m_services.notifications->pauseExpiry(n.id);
    m_state.hold(IslandEvent::Notification);
    m_surface.requestUpdate();
    return true;
  }
  if (event == NotificationEvent::Updated && stored.id != n.id)
    return false;
  stored = n;
  if (call && n.timeout == 0)
    m_state.hold(kind);
  else
    m_state.push(kind, nowMs(), m_config.dynamicDwellMs);
  m_surface.requestUpdate();
  return true;
}

void DynamicIsland::syncActivities() {
  auto* a = m_services.activities;
  if (!a)
    return;
  const bool timer = m_config.dynamicTimers && a->timerTarget == m_output;
  const bool alarm = m_config.dynamicAlarms && a->alarmTarget == m_output;
  if (!timer || (!a->timerEnd && !a->timerFinished)) {
    m_state.clear(IslandEvent::Timer);
    m_state.clear(IslandEvent::TimerFinished);
  } else if (m_timerRevision != a->timerRevision || !m_initialized) {
    m_state.clear(IslandEvent::Timer);
    m_state.clear(IslandEvent::TimerFinished);
    m_state.hold(a->timerFinished ? IslandEvent::TimerFinished : IslandEvent::Timer);
  }
  if (!alarm || !a->alarmRinging)
    m_state.clear(IslandEvent::Alarm);
  else if (m_alarmRevision != a->alarmRevision || !m_initialized)
    m_state.hold(IslandEvent::Alarm);
  m_timerRevision = a->timerRevision;
  m_alarmRevision = a->alarmRevision;
  m_surface.requestUpdate();
}

bool DynamicIsland::activityCommand(const std::string& command, const std::string& value) {
  auto* a = m_services.activities;
  if (!a)
    return false;
  if (command == "timer-start" && m_config.dynamicTimers) {
    int seconds = m_config.dynamicTimerSeconds;
    if (!value.empty()) {
      try {
        std::size_t end;
        seconds = std::stoi(value, &end);
        if (end != value.size())
          return false;
      } catch (...) {
        return false;
      }
    }
    if (seconds < 1 || seconds > 86400)
      return false;
    a->startTimer(seconds, m_output);
    return true;
  }
  if (command == "timer-cancel") {
    a->cancelTimer();
    return true;
  }
  if (command == "alarm-arm" && m_config.dynamicAlarms)
    return a->armAlarm(value.empty() ? m_config.dynamicAlarmTime : value, m_output);
  if (command == "alarm-dismiss") {
    a->dismissAlarm();
    return true;
  }
  if (command == "alarm-snooze" && a->alarmRinging) {
    a->snooze(m_config.dynamicSnoozeSeconds);
    return true;
  }
  return false;
}
