#include "shell/bar/dynamic_island.h"

#include "config/config_service.h"
#include "dbus/mpris/mpris_art.h"
#include "dbus/mpris/mpris_service.h"
#include "pipewire/pipewire_service.h"
#include "render/core/renderer.h"
#include "render/scene/screen_frame_node.h"
#include "shell/bar/screen_wrap.h"
#include "system/brightness_service.h"
#include "time/time_format.h"
#include "ui/builders.h"
#include "ui/palette.h"
#include "wayland/wayland_seat.h"

#include <algorithm>
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
  const bool side = vertical(c);
  const int gap = c.screenWrap ? 0 : std::clamp(c.marginEdge, 0, std::min(outputWidth, outputHeight) / 4);
  const int width = std::min(outputWidth - gap, static_cast<int>(static_cast<float>(c.dynamicWidth) + flare(c) * 2));
  const int height = std::min(outputHeight - gap, static_cast<int>(220 + flare(c) * 2));
  const bool bottom = c.position == "bottom", right = c.position == "right";
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
      .prewarmBlur = c.compositorBlur,
  };
}

DynamicIsland::DynamicIsland(
    const BarServices& services, const BarConfig& config, LayerSurface& surface, AnimationManager& animations,
    ScreenWrap* wrap, wl_output* output, int outputWidth, int outputHeight
)
    : m_services(services), m_config(config), m_surface(surface), m_animations(animations), m_wrap(wrap),
      m_output(output), m_outputWidth(outputWidth), m_outputHeight(outputHeight), m_vertical(vertical(config)),
      m_reverse(config.position == "bottom" || config.position == "right") {
  m_paletteConnection = paletteChanged().connect([this] { m_surface.requestUpdate(); });
}

DynamicIsland::~DynamicIsland() { m_alive.reset(); }

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
  body->setOnClick([this](const auto&) { toggleExpanded(); });
  m_body = static_cast<InputArea*>(m_root->addChild(std::move(body)));
  m_content = m_body->addChild(ui::node({}));
  m_content->addChild(ui::glyph({.out = &m_glyph, .glyph = "clock"}));
  m_content->addChild(ui::label({.out = &m_title}));
  m_content->addChild(ui::label({.out = &m_detail}));
  m_content->addChild(ui::image({.out = &m_art, .fit = ImageFit::Cover}));
  m_previous = static_cast<Button*>(m_content->addChild(
      ui::button({
          .glyph = "player-skip-back",
          .variant = ButtonVariant::Ghost,
          .onClick = [this] {
            if (m_services.mpris)
              m_services.mpris->previousActive();
          },
      })
  ));
  m_play = static_cast<Button*>(m_content->addChild(
      ui::button({
          .glyph = "player-play",
          .variant = ButtonVariant::Ghost,
          .onClick = [this] {
            if (m_services.mpris)
              m_services.mpris->playPauseActive();
          },
      })
  ));
  m_next = static_cast<Button*>(m_content->addChild(
      ui::button({
          .glyph = "player-skip-forward",
          .variant = ButtonVariant::Ghost,
          .onClick = [this] {
            if (m_services.mpris)
              m_services.mpris->nextActive();
          },
      })
  ));
  m_close = static_cast<Button*>(m_content->addChild(
      ui::button({
          .glyph = "x",
          .variant = ButtonVariant::Ghost,
          .onClick = [this] {
            m_pinned = false;
            m_hoverBlocked = true;
            m_surface.requestUpdate();
          },
      })
  ));
  auto level = ui::inputArea({});
  level->setOnClick([this](const auto& p) {
    setLevel(std::clamp(p.localX / std::max(1.0F, m_levelArea->width()), 0.0F, 1.0F));
  });
  m_levelArea = static_cast<InputArea*>(m_content->addChild(std::move(level)));
  auto progress = std::make_unique<ProgressBar>();
  m_progress = static_cast<ProgressBar*>(m_levelArea->addChild(std::move(progress)));
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
  const auto player = m_services.mpris ? m_services.mpris->activePlayer() : std::nullopt;
  const bool expand = m_pinned || (m_config.dynamicHoverExpand && m_hovered && !m_hoverBlocked);
  auto kind = m_state.resolve(nowMs(), (m_hovered && !m_hoverBlocked) || m_pinned ? m_event : IslandEvent::Resting);
  if (kind == IslandEvent::Media && (!player || !m_config.dynamicMedia))
    kind = IslandEvent::Resting;
  if (kind == IslandEvent::Resting && expand && player && m_config.dynamicMedia)
    kind = IslandEvent::Media;
  const bool changed = kind != m_event || expand != m_expanded;
  m_event = kind;
  m_expanded = expand;
  if (changed || !m_initialized)
    changeContent(kind, expand);
  float w = m_vertical ? static_cast<float>(m_config.thickness) : kind == IslandEvent::Resting ? 112.0F : 280.0F;
  float h = m_vertical ? 76.0F : static_cast<float>(m_config.thickness);
  if (expand) {
    w = static_cast<float>(m_config.dynamicWidth);
    h = kind == IslandEvent::Media ? 190.0F : kind == IslandEvent::Resting ? 110.0F : 120.0F;
  }
  w = std::min(w, std::max(1.0F, static_cast<float>(m_surface.width()) - flare(m_config) * 2));
  h = std::min(h, std::max(1.0F, static_cast<float>(m_surface.height()) - flare(m_config)));
  if (!m_initialized) {
    m_width = w;
    m_height = h;
    m_initialized = true;
  }
  if (w != m_targetWidth || h != m_targetHeight) {
    m_animations.cancel(m_resizeAnimation);
    m_fromWidth = m_width;
    m_fromHeight = m_height;
    m_targetWidth = w;
    m_targetHeight = h;
    m_resizeAnimation = m_animations.animate(
        0, 1, 300, Easing::EaseOutCubic,
        [this](float t) {
          m_width = m_fromWidth + (m_targetWidth - m_fromWidth) * t;
          m_height = m_fromHeight + (m_targetHeight - m_fromHeight) * t;
          m_surface.requestLayout();
        },
        {}, m_root.get()
    );
    m_surface.requestRedraw();
  }
  m_expiryTimer.stop();
  const auto remaining = m_state.remaining(m_event, nowMs());
  if (m_event != IslandEvent::Resting && remaining > 0)
    m_expiryTimer.start(std::chrono::milliseconds(remaining + 1), [this] { m_surface.requestUpdate(); });
}

void DynamicIsland::changeContent(IslandEvent kind, bool expanded) {
  if (!m_initialized) {
    m_displayEvent = kind;
    m_displayExpanded = expanded;
    return;
  }
  m_animations.cancelForOwner(m_content);
  m_animations.animate(
      m_content->opacity(), 0, 90, Easing::EaseOutCubic, [this](float v) { m_content->setOpacity(v); },
      [this, kind, expanded] {
        m_displayEvent = kind;
        m_displayExpanded = expanded;
        m_surface.requestLayout();
        m_animations.animate(
            0, 1, 210, Easing::EaseOutCubic, [this](float v) { m_content->setOpacity(v); }, {}, m_content
        );
      },
      m_content
  );
  m_surface.requestRedraw();
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
  m_background->setPosition(x, y);
  m_background->setSize(m_width, m_height);
  m_background->setFill(colorForRole(ColorRole::Surface, m_wrap ? 0.0F : m_config.backgroundOpacity));
  m_background->setBorder(resolveColorSpec(m_config.border), m_wrap ? 0.0F : m_config.borderWidth);
  m_background->setRadius(static_cast<float>(m_config.radius));
  if (m_wrap) {
    const float begin = m_vertical ? m_originY + y : m_originX + x;
    m_wrap->setIslands(
        {ScreenFrameSpan{}, ScreenFrameSpan{begin, begin + (m_vertical ? m_height : m_width)}, ScreenFrameSpan{}},
        m_vertical ? m_width : m_height
    );
    m_wrap->setForegroundHole(
        {static_cast<int>(m_originX), static_cast<int>(m_originY), static_cast<int>(sw), static_cast<int>(sh)}
    );
    m_frameClip->setPosition(0, 0);
    m_frameClip->setSize(sw, sh);
    m_frame->setPosition(0, 0);
    m_frame->setSize(sw, sh);
    m_frame->setStyle(
        {m_wrap->contour(), colorForRole(ColorRole::Surface, m_config.backgroundOpacity),
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
  const auto player = m_services.mpris ? m_services.mpris->activePlayer() : std::nullopt;
  const bool media = m_displayEvent == IslandEvent::Media && player.has_value();
  const bool level = m_displayEvent == IslandEvent::Volume || m_displayEvent == IslandEvent::Brightness;
  const bool expanded = m_displayExpanded;
  const float fontScale = m_config.scale * m_config.fontScale;
  const std::string font = m_config.fontFamily.value_or(m_services.config.config().shell.fontFamily);
  const auto& feedback = m_feedback[static_cast<unsigned>(m_displayEvent)];
  std::string title = m_displayEvent == IslandEvent::Resting ? formatLocalTime(m_config.dynamicClockFormat.c_str())
      : media ? (player->title.empty() ? player->identity : player->title)
              : feedback.value;
  if (!expanded && m_vertical) {
    if (m_displayEvent == IslandEvent::Resting)
      std::ranges::replace(title, ':', '\n');
    else if (media)
      title = "";
  }
  m_title->setText(title);
  m_title->setFontFamily(font);
  m_title->setFontSize((m_displayEvent == IslandEvent::Resting && expanded ? 26.0F : 16.0F) * fontScale);
  m_title->setColor(colorSpecFromRole(ColorRole::OnSurface));
  m_title->setMaxWidth(
      std::max(
          1.0F,
          m_width
              - (expanded                                     ? 92.0F
                     : m_vertical                             ? 12.0F
                     : m_displayEvent == IslandEvent::Resting ? 24.0F
                                                              : 60.0F)
      )
  );
  m_title->setMaxLines(!expanded && m_vertical ? 1 + static_cast<int>(std::ranges::count(title, '\n')) : 1);
  m_title->setFontWeight(static_cast<FontWeight>(m_config.fontWeight));
  m_title->measure(renderer);
  m_detail->setText(
      media                                           ? mpris::joinArtists(player->artists)
          : m_displayEvent == IslandEvent::Resting    ? formatLocalTime("%A, %d %B")
          : m_displayEvent == IslandEvent::Brightness ? "Brightness"
          : feedback.kind == OsdKind::Microphone      ? "Microphone"
                                                      : "Volume"
  );
  m_detail->setFontFamily(font);
  m_detail->setFontSize(13 * fontScale);
  m_detail->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
  m_detail->setMaxWidth(std::max(1.0F, m_width - 100));
  m_detail->measure(renderer);
  m_detail->setVisible(expanded);
  m_glyph->setGlyph(media ? "disc" : m_displayEvent == IslandEvent::Resting ? "clock" : feedback.icon);
  m_glyph->setGlyphSize(20 * m_config.scale);
  m_glyph->setColor(colorSpecFromRole(ColorRole::Primary));
  m_glyph->measure(renderer);
  m_glyph->setVisible(m_displayEvent != IslandEvent::Resting && !(media && expanded));
  m_art->setVisible(media && expanded);
  m_art->setSize(58, 58);
  m_art->setRadius(10);
  if (media && expanded) {
    const auto path = mpris::resolveArtworkSource(
        m_services.httpClient, m_pendingArt, mpris::effectiveArtUrl(*player), [this] { m_surface.requestUpdate(); },
        m_alive
    );
    if (!path.empty() && path != m_artPath) {
      m_artPath = path;
      m_art->setSourceFile(renderer, path, 116);
    }
    m_art->setVisible(!path.empty());
    m_glyph->setVisible(path.empty());
  }
  if (expanded) {
    const float textX = media ? 86.0F : m_displayEvent == IslandEvent::Resting ? 22.0F : 60.0F;
    m_art->setPosition(16, 18);
    m_glyph->setPosition(20, 25);
    m_title->setPosition(textX, 20);
    m_detail->setPosition(textX, m_displayEvent == IslandEvent::Resting ? 64.0F : 48.0F);
    m_levelArea->setPosition(22, media ? 86.0F : 78.0F);
    m_levelArea->setSize(std::max(1.0F, m_width - 44), 24);
  } else if (m_vertical) {
    m_glyph->setPosition((m_width - m_glyph->width()) / 2, media ? (m_height - m_glyph->height()) / 2 : 10);
    m_title->setPosition(
        (m_width - m_title->width()) / 2,
        m_displayEvent == IslandEvent::Resting ? (m_height - m_title->height()) / 2 : m_height - m_title->height() - 9
    );
  } else {
    m_glyph->setPosition(16, (m_height - m_glyph->height()) / 2);
    m_title->setPosition(
        m_displayEvent == IslandEvent::Resting ? (m_width - m_title->width()) / 2 : 48,
        (m_height - m_title->height()) / 2
    );
  }
  if (media)
    m_play->setGlyph(player->playbackStatus == "Playing" ? "player-pause" : "player-play");
  for (auto* button : {m_previous, m_play, m_next, m_close}) {
    button->setVisible(expanded && (button == m_close || media));
    button->setGlyphSize(20 * m_config.scale);
    button->setControlHeight(36);
    button->layout(renderer);
  }
  m_close->setPosition(std::max(0.0F, m_width - m_close->width() - 8), 6);
  if (media) {
    m_play->setEnabled(player->canPlay || player->canPause);
    m_previous->setEnabled(player->canGoPrevious);
    m_next->setEnabled(player->canGoNext);
    m_play->setPosition((m_width - m_play->width()) / 2, 124);
    m_previous->setPosition(m_play->x() - m_previous->width() - 16, 124);
    m_next->setPosition(m_play->x() + m_play->width() + 16, 124);
  }
  m_levelArea->setVisible(expanded && (media || level));
  m_progress->setSize(m_levelArea->width(), 5);
  m_progress->setPosition(0, 10);
  m_progress->setFill(colorSpecFromRole(ColorRole::Primary));
  m_progress->setTrack(colorSpecFromRole(ColorRole::SurfaceContainerHighest));
  updateProgress();
  if (media && expanded && m_visible && player->playbackStatus == "Playing") {
    if (!m_progressTimer.active())
      m_progressTimer.startRepeating(std::chrono::seconds(1), [this] { m_surface.requestLayout(); });
  } else
    m_progressTimer.stop();
}

void DynamicIsland::updateProgress() {
  if (m_displayEvent == IslandEvent::Media && m_services.mpris) {
    const auto player = m_services.mpris->activePlayer();
    if (player && player->lengthUs > 0)
      m_progress->setProgress(
          static_cast<float>(m_services.mpris->positionActive().value_or(player->positionUs))
          / static_cast<float>(player->lengthUs)
      );
    else
      m_progress->setProgress(0.0F);
  } else
    m_progress->setProgress(std::clamp(m_feedback[static_cast<unsigned>(m_displayEvent)].progress, 0.0F, 1.0F));
}

void DynamicIsland::setLevel(float fraction) {
  if (m_displayEvent == IslandEvent::Media && m_services.mpris) {
    const auto player = m_services.mpris->activePlayer();
    if (player && player->canSeek && player->lengthUs > 0)
      m_services.mpris->setPositionActive(
          static_cast<std::int64_t>(static_cast<double>(player->lengthUs) * static_cast<double>(fraction))
      );
  } else if (m_displayEvent == IslandEvent::Brightness && m_services.brightness) {
    if (const auto* display = m_services.brightness->findByOutput(m_output))
      m_services.brightness->setBrightness(display->id, fraction);
  } else if (m_displayEvent == IslandEvent::Volume && m_services.audio) {
    if (m_feedback[static_cast<unsigned>(m_displayEvent)].kind == OsdKind::Microphone)
      m_services.audio->setMicVolume(fraction);
    else
      m_services.audio->setVolume(fraction);
  }
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

void DynamicIsland::toggleExpanded() {
  if (m_pinned) {
    m_pinned = false;
    m_hoverBlocked = true;
  } else {
    m_pinned = true;
    m_hoverBlocked = false;
  }
  m_surface.requestUpdate();
}

bool DynamicIsland::onPointerEvent(const PointerEvent& e) {
  if (!m_visible)
    return false;
  switch (e.type) {
  case PointerEvent::Type::Enter:
    m_hoverTimer.stop();
    m_hovered = true;
    m_dispatcher.pointerEnter(static_cast<float>(e.sx), static_cast<float>(e.sy), e.serial);
    m_surface.requestUpdate();
    return true;
  case PointerEvent::Type::Leave:
    m_dispatcher.pointerLeave();
    m_hoverTimer.start(std::chrono::milliseconds(250), [this] {
      m_hovered = false;
      m_hoverBlocked = false;
      m_surface.requestUpdate();
    });
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
    m_hovered = m_pinned = false;
    m_hoverTimer.stop();
    m_progressTimer.stop();
  }
  m_surface.requestUpdate();
}
