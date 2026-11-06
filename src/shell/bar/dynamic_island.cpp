#include "shell/bar/dynamic_island.h"

#include "config/config_service.h"
#include "core/deferred_call.h"
#include "dbus/mpris/mpris_art.h"
#include "dbus/mpris/mpris_service.h"
#include "render/core/renderer.h"
#include "render/scene/screen_frame_node.h"
#include "shell/bar/island_activities.h"
#include "shell/bar/dynamic_island_style.h"
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
  const int gap = c.screenWrap ? 0 : std::clamp(c.marginEdge, 0, std::min(outputWidth, outputHeight) / 4);
  // Only the narrow bar strip grows. The existing panel host owns the native tool's surface.
  const int strip = c.thickness + static_cast<int>(flare(c));
  const int width = side ? std::min(strip, outputWidth - gap) : outputWidth;
  const int height = side ? outputHeight : std::min(strip, outputHeight - gap);
  return {
      .nameSpace = "cyberpunk-dynamic-island-" + c.name,
      .layer = layerShellLayerFromConfig(c.layer),
      .anchor = side ? (right ? LayerShellAnchor::Right : LayerShellAnchor::Left)
                     : (bottom ? LayerShellAnchor::Bottom : LayerShellAnchor::Top),
      .width = static_cast<std::uint32_t>(std::max(1, width)),
      .height = static_cast<std::uint32_t>(std::max(1, height)),
      .exclusiveZone = c.reserveSpace ? c.thickness : 0,
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
  m_paletteConnection = paletteChanged().connect([this] { m_surface.requestUpdate(); });
  if (m_services.activities) {
    m_activityConnection = m_services.activities->changed.connect([this] { syncActivities(); });
    syncActivities();
  }
}
DynamicIsland::~DynamicIsland() {
  m_alive.reset();
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
  case IslandEvent::Notification:
  case IslandEvent::Call:
    return "notifications";
  default:
    return "home";
  }
}
float DynamicIsland::compactExtent(IslandEvent kind) const {
  if (m_vertical) {
    if (kind == IslandEvent::Call)
      return 156.0F;
    if (kind == IslandEvent::Alarm)
      return 124.0F;
    if (kind == IslandEvent::Timer || kind == IslandEvent::TimerFinished)
      return 112.0F;
    return 76.0F;
  }
  return kind == IslandEvent::Resting ? 112.0F : 280.0F;
}

void DynamicIsland::build() {
  m_root = ui::node({});
  m_root->setAnimationManager(&m_animations);
  m_background = static_cast<Box*>(m_root->addChild(ui::box()));
  if (m_wrap) {
    auto clip = ui::node({});
    clip->setClipChildren(true);
    clip->setHitTestVisible(false);
    m_frameClip = m_root->addChild(std::move(clip));
    auto frame = std::make_unique<ScreenFrameNode>();
    frame->setHitTestVisible(false);
    m_frame = static_cast<ScreenFrameNode*>(m_frameClip->addChild(std::move(frame)));
  }
  auto body = ui::inputArea({});
  body->setClipChildren(true);
  body->setOnClick([this](const auto&) { openNativeTool(); });
  m_body = static_cast<InputArea*>(m_root->addChild(std::move(body)));
  m_content = m_body->addChild(ui::node({}));
  m_content->addChild(ui::glyph({.out = &m_glyph, .glyph = "clock"}));
  m_content->addChild(ui::label({.out = &m_title}));
  m_content->addChild(ui::image({.out = &m_art, .fit = ImageFit::Cover}));
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
  else
    return false;
  m_feedback[static_cast<unsigned>(kind)] = content;
  m_state.push(kind, nowMs(), m_config.dynamicDwellMs);
  m_surface.requestUpdate();
  return true;
}

void DynamicIsland::resolve() {
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
  m_content->setOpacity(1.0F - m_nativeProgress);
  m_expiryTimer.stop();
  const auto remaining = m_state.remaining(kind, nowMs());
  if (kind != IslandEvent::Resting && remaining > 0)
    m_expiryTimer.start(std::chrono::milliseconds(remaining + 1), [this] { m_surface.requestUpdate(); });
}
void DynamicIsland::changeContent(IslandEvent kind) {
  m_displayEvent = kind;
  // Native-panel content owns its own reveal; compact event changes simply refresh in place.
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
  const float gap = m_config.screenWrap
      ? 0.0F
      : static_cast<float>(std::clamp(m_config.marginEdge, 0, std::min(m_outputWidth, m_outputHeight) / 4));
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
  const float extension = dynamic_island_style::joinExtension(m_nativeProgress, static_cast<float>(m_config.radius), m_config.borderWidth);
  const float panelOpacity = detachedPanelBackgroundOpacityForTransparencyMode(m_services.config.config().shell.panel.transparencyMode);
  const float opacity = std::lerp(m_config.backgroundOpacity, panelOpacity, m_nativeProgress);
  m_background->setPosition(x - (m_vertical && m_reverse ? extension : 0), y - (!m_vertical && m_reverse ? extension : 0));
  m_background->setSize(m_width + (m_vertical ? extension : 0), m_height + (m_vertical ? 0 : extension));
  m_background->setFill(colorForRole(ColorRole::Surface, m_wrap ? 0.0F : opacity));
  m_background->setBorder(resolveColorSpec(m_config.border), m_wrap ? 0.0F : m_config.borderWidth);
  m_background->setRadius(static_cast<float>(m_config.radius));
  if (m_wrap) {
    const float begin = m_vertical ? m_originY + y : m_originX + x;
    m_wrap->setIslands(
        {ScreenFrameSpan{}, ScreenFrameSpan{begin, begin + (m_vertical ? m_height : m_width)}, ScreenFrameSpan{}},
        (m_vertical ? m_width : m_height) + extension
    );
    m_wrap->setForegroundHole(
        {static_cast<int>(m_originX), static_cast<int>(m_originY), static_cast<int>(sw), static_cast<int>(sh)}
    );
    m_frameClip->setPosition(0, 0);
    m_frameClip->setSize(sw, sh);
    m_frame->setPosition(0, 0);
    m_frame->setSize(sw, sh);
    m_frame->setStyle(
        {m_wrap->contour(), colorForRole(ColorRole::Surface, opacity),
         resolveColorSpec(m_config.border), m_config.borderWidth, m_originX, m_originY}
    );
  }
  if (!m_visible)
    m_surface.setInputRegion({});
  else
    m_surface.setInputRegion(
        Surface::tessellateRoundedRect(
            static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), static_cast<int>(std::ceil(m_width)),
            static_cast<int>(std::ceil(m_height)), static_cast<float>(m_config.radius)
        )
    );
  if (!m_config.compositorBlur || !m_visible)
    m_surface.clearBlurRegion();
  else {
    auto blur = Surface::tessellateRoundedRect(
        static_cast<int>(x), static_cast<int>(y), static_cast<int>(m_width), static_cast<int>(m_height),
        static_cast<float>(m_config.radius)
    );
    if (m_wrap)
      blur = m_wrap->blurRegion(
          {static_cast<int>(m_originX), static_cast<int>(m_originY), static_cast<int>(sw), static_cast<int>(sh)}
      );
    m_surface.setBlurRegion(blur);
  }
}

void DynamicIsland::layout(Renderer& renderer) {
  const auto kind = m_displayEvent;
  const auto player = m_services.mpris ? m_services.mpris->activePlayer() : std::nullopt;
  const bool media = kind == IslandEvent::Media && player.has_value();
  const bool level = kind == IslandEvent::Volume || kind == IslandEvent::Brightness;
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
  if (m_vertical && !level && kind != IslandEvent::Resting && kind != IslandEvent::Timer)
    title = "";
  const float fontScale = m_config.scale * m_config.fontScale;
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
  m_glyph->setGlyphSize(20 * m_config.scale);
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
      m_art->setSourceFile(renderer, path, 64);
    }
    if (!path.empty()) {
      m_art->setVisible(true);
      m_art->setSize(28, 28);
      m_art->setRadius(6);
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
    button->setControlHeight(30);
    button->setGlyphSize(18);
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
        m_vertical ? m_height - static_cast<float>(count - i) * 32 : (m_height - b->height()) / 2
    );
  }
  m_title->setMaxWidth(
      std::max(
          1.0F,
          m_width
              - (m_vertical                         ? 10
                     : kind == IslandEvent::Resting ? 20
                                                    : 60)
              - actionSpace
      )
  );
  m_title->measure(renderer);
  if (m_vertical) {
    m_glyph->setPosition((m_width - m_glyph->width()) / 2, 10);
    m_art->setPosition((m_width - 28) / 2, 10);
    m_title->setPosition(
        (m_width - m_title->width()) / 2, kind == IslandEvent::Resting ? (m_height - m_title->height()) / 2 : 40
    );
  } else {
    m_glyph->setPosition(16, (m_height - m_glyph->height()) / 2 - (level ? 3 : 0));
    m_art->setPosition(12, (m_height - 28) / 2);
    m_title->setPosition(
        kind == IslandEvent::Resting ? (m_width - m_title->width()) / 2 : 48,
        (m_height - m_title->height()) / 2 - (level ? 4 : 0)
    );
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
void DynamicIsland::openNativeTool() {
  auto* panels = PanelManager::current();
  if (!panels || !m_visible || m_hoverBlocked)
    return;
  if (panels->isDynamicIslandPanel(m_output, m_config.name))
    return;
  panels->openPanel(
      "control-center",
      {.output = m_output,
       .anchorX = static_cast<float>(m_outputWidth) / 2,
       .anchorY = static_cast<float>(m_outputHeight) / 2,
       .hasExplicitAnchor = true,
       .hasAnchorPosition = true,
       .context = nativeToolContext(m_event),
       .sourceBarName = m_config.name,
       .fromDynamicIsland = true,
       .islandCompactExtent = compactExtent(m_event)}
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
  if (!geometry && m_nativeOwned) {
    m_nativeOwned = false;
    m_hoverBlocked = m_hovered;
  }
  m_surface.requestUpdate();
}
void DynamicIsland::watchNativeLeave() {
  m_hoverTimer.start(std::chrono::milliseconds(250), [this] {
    auto* panels = PanelManager::current();
    if (!panels || !panels->isDynamicIslandPanel(m_output, m_config.name))
      return;
    if (m_hovered || panels->pointerHoldsPanel()) {
      watchNativeLeave();
      return;
    }
    panels->closePanel();
  });
}
bool DynamicIsland::onPointerEvent(const PointerEvent& e) {
  if (!m_visible)
    return false;
  switch (e.type) {
  case PointerEvent::Type::Enter:
    m_hovered = true;
    m_hoverTimer.stop();
    m_dispatcher.pointerEnter(static_cast<float>(e.sx), static_cast<float>(e.sy), e.serial);
    if (m_config.dynamicHoverExpand)
      openNativeTool();
    return true;
  case PointerEvent::Type::Leave:
    m_hovered = false;
    m_hoverBlocked = false;
    m_dispatcher.pointerLeave();
    watchNativeLeave();
    return true;
  case PointerEvent::Type::Motion:
    m_dispatcher.pointerMotion(static_cast<float>(e.sx), static_cast<float>(e.sy), e.serial);
    return true;
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
    if (auto* panels = PanelManager::current(); panels && panels->isDynamicIslandPanel(m_output, m_config.name))
      panels->closePanel(false);
  }
  m_surface.requestUpdate();
}
void DynamicIsland::dismissEvent() {
  if (m_event == IslandEvent::Call && m_services.notifications)
    (void)m_services.notifications->close(m_notices[1].id, CloseReason::Dismissed);
  else if (m_services.activities) {
    if (m_event == IslandEvent::Alarm)
      m_services.activities->dismissAlarm();
    else
      m_services.activities->cancelTimer();
  }
}

bool DynamicIsland::notification(const Notification& n, NotificationEvent event) {
  const bool call = isIslandCall(n);
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
