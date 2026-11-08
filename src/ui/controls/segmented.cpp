#include "ui/controls/segmented.h"

#include "render/core/render_styles.h"
#include "render/scene/input_area.h"
#include "ui/controls/box.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/roving_list_nav.h"
#include "ui/controls/separator.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

Segmented::Segmented() {
  setDirection(FlexDirection::Horizontal);
  setAlign(FlexAlign::Stretch);
  setGap(0.0F);
  applyOuterStyle();

  auto area = std::make_unique<InputArea>();
  area->setFocusable(true);
  area->setHitTestVisible(false);
  m_focusArea = static_cast<InputArea*>(addChild(std::move(area)));
  m_focusArea->setParticipatesInLayout(false);
  m_focusArea->setZIndex(2);

  m_rovingNav.setOptions(
      RovingListNavController::Options{
          .axis = RovingListNavAxis::Horizontal,
          .mode = RovingListNavMode::FollowFocus,
          .scrollIntoView = {},
          .syncIndexFromSelection = [this] { return m_selected; },
      }
  );
  m_rovingNav.bindFocusArea(m_focusArea);
}

std::size_t Segmented::addOption(std::string_view label) { return addOption(label, std::string_view{}); }

std::size_t Segmented::addOption(std::string_view label, std::string_view glyph) {
  const std::size_t index = m_buttons.size();
  if (index > 0) {
    auto sep = makeSegmentSeparator();
    m_separators.push_back(sep.get());
    addChild(std::move(sep));
  }
  auto btn = makeSegmentButton(label, glyph, index);
  Button* raw = btn.get();
  m_buttons.push_back(raw);
  m_rovingNav.registerItem(raw, [this, index] { setSelectedIndex(index); });
  addChild(std::move(btn));
  refreshVariants();
  return index;
}

void Segmented::setSelectedIndex(std::size_t index) {
  if (index >= m_buttons.size() || index == m_selected) {
    return;
  }
  m_selected = index;
  refreshVariants();
  m_rovingNav.notifyExternalSelectionChanged();
  if (m_onChange) {
    m_onChange(index);
  }
}

void Segmented::setQuietActiveStyle(bool enabled) {
  if (m_quietActiveStyle == enabled) {
    return;
  }
  m_quietActiveStyle = enabled;
  if (enabled) {
    auto underline = std::make_unique<Box>();
    m_activeUnderline = underline.get();
    m_activeUnderline->setFill(colorSpecFromRole(ColorRole::Primary));
    m_activeUnderline->setZIndex(1);
    m_activeUnderline->setParticipatesInLayout(false);
    addChild(std::move(underline));
  } else if (m_activeUnderline != nullptr) {
    (void)removeChild(m_activeUnderline);
    m_activeUnderline = nullptr;
    m_underlineSelection.reset();
  }
  refreshVariants();
  markLayoutDirty();
}

void Segmented::setFontSize(float size) {
  m_fontSize = size;
  const float fs = effectiveFontSize();
  for (Button* btn : m_buttons) {
    if (btn != nullptr) {
      btn->setFontSize(fs);
      btn->setGlyphSize(fs);
    }
  }
}

void Segmented::setScale(float scale) {
  m_scale = std::max(0.1F, scale);
  applyOuterStyle();
  const float fs = effectiveFontSize();
  for (Button* btn : m_buttons) {
    if (btn != nullptr) {
      applyButtonMetrics(*btn);
      btn->setFontSize(fs);
      btn->setGlyphSize(fs);
    }
  }
  const float ruleW = std::max(1.0F, Style::borderWidth * m_scale);
  for (Separator* sep : m_separators) {
    if (sep != nullptr) {
      sep->setThickness(ruleW);
    }
  }
  refreshVariants();
  markLayoutDirty();
}

void Segmented::setCompact(bool compact) {
  if (m_compact == compact) {
    return;
  }
  m_compact = compact;
  for (Button* btn : m_buttons) {
    if (btn != nullptr) {
      applyButtonMetrics(*btn);
    }
  }
  markLayoutDirty();
}

void Segmented::setPadding(float padding) {
  m_outerPadding = padding;
  Flex::setPadding(padding);
}

void Segmented::setPadding(float vertical, float horizontal) {
  m_outerPadding = vertical;
  Flex::setPadding(vertical, horizontal);
}

void Segmented::setPadding(float top, float right, float bottom, float left) {
  m_outerPadding = top;
  Flex::setPadding(top, right, bottom, left);
}

void Segmented::setOptionTooltip(std::size_t index, std::string_view text) {
  if (index < m_buttons.size() && m_buttons[index] != nullptr) {
    m_buttons[index]->setTooltip(text);
  }
}

void Segmented::clearOptions() {
  for (Button* btn : m_buttons) {
    if (btn != nullptr) {
      (void)removeChild(btn);
    }
  }
  for (Separator* sep : m_separators) {
    if (sep != nullptr) {
      (void)removeChild(sep);
    }
  }
  m_buttons.clear();
  m_separators.clear();
  m_rovingNav.clearItems();
  m_selected = 0;
  markLayoutDirty();
}

void Segmented::setOnChange(std::function<void(std::size_t)> callback) { m_onChange = std::move(callback); }

void Segmented::setSurfaceOpacity(float opacity) {
  const float clamped = std::clamp(opacity, 0.0F, 1.0F);
  if (m_surfaceOpacity == clamped) {
    return;
  }
  m_surfaceOpacity = clamped;
  applyOuterStyle();
}

void Segmented::setSurfaceRole(ColorRole role) {
  if (m_surfaceRole == role) {
    return;
  }
  m_surfaceRole = role;
  applyOuterStyle();
}

void Segmented::setEnabled(bool enabled) {
  if (m_enabled == enabled) {
    return;
  }
  m_enabled = enabled;
  for (Button* btn : m_buttons) {
    if (btn != nullptr) {
      btn->setEnabled(enabled);
    }
  }
  setOpacity(enabled ? 1.0F : 0.55F);
}

std::unique_ptr<Separator> Segmented::makeSegmentSeparator() {
  auto sep = std::make_unique<Separator>();
  sep->setOrientation(SeparatorOrientation::VerticalRule);
  sep->setThickness(std::max(1.0F, Style::borderWidth * m_scale));
  sep->setColor(colorSpecFromRole(ColorRole::OutlineVariant));
  sep->setFlexGrow(0.0F);
  return sep;
}

std::unique_ptr<Button>
Segmented::makeSegmentButton(std::string_view label, std::string_view glyph, std::size_t index) {
  auto btn = std::make_unique<Button>();
  if (!glyph.empty()) {
    btn->setGlyph(glyph);
    btn->setGlyphSize(effectiveFontSize());
  }
  if (!label.empty()) {
    btn->setText(label);
    btn->setFontSize(effectiveFontSize());
  }
  applyButtonMetrics(*btn);
  btn->setOnClick([this, index] { setSelectedIndex(index); });
  btn->setTabStop(false);
  btn->setFlexGrow(m_equalSegmentWidths ? 1.0F : 0.0F);
  btn->setContentAlign(ButtonContentAlign::Center);
  btn->setEnabled(m_enabled);
  return btn;
}

void Segmented::applyButtonMetrics(Button& button) const {
  if (m_compact) {
    button.setMinHeight(Style::controlHeightSm * m_scale);
    button.setPadding(Style::spaceXs * m_scale, Style::spaceSm * m_scale);
    return;
  }

  button.setMinHeight(Style::controlHeight * m_scale);
  button.setPadding(Style::spaceXs * m_scale, Style::spaceMd * m_scale);
}

void Segmented::setEqualSegmentWidths(bool equalWidths) {
  if (m_equalSegmentWidths == equalWidths) {
    return;
  }
  m_equalSegmentWidths = equalWidths;
  for (Button* b : m_buttons) {
    if (b != nullptr) {
      b->setFlexGrow(m_equalSegmentWidths ? 1.0F : 0.0F);
    }
  }
  markLayoutDirty();
}

void Segmented::refreshVariants() {
  const std::size_t n = m_buttons.size();
  const float r = Style::scaledRadiusMd(m_scale);
  for (std::size_t i = 0; i < n; ++i) {
    if (m_buttons[i] == nullptr) {
      continue;
    }
    m_buttons[i]->setVariant(i == m_selected ? ButtonVariant::TabActive : ButtonVariant::Tab);
    if (m_quietActiveStyle && i == m_selected) {
      const auto state = [](ColorSpec bg, ColorSpec label) {
        return Button::ButtonStateColors{.bg = std::move(bg), .border = clearColorSpec(), .label = std::move(label)};
      };
      m_buttons[i]->setCustomPalette(
          Button::ButtonPalette{
              .borderWidth = 0.0F,
              .normal = state(colorSpecFromRole(ColorRole::Primary, 0.14F), colorSpecFromRole(ColorRole::OnSurface)),
              .hover = state(colorSpecFromRole(ColorRole::Hover), colorSpecFromRole(ColorRole::OnHover)),
              .pressed = state(colorSpecFromRole(ColorRole::SurfaceContainer), colorSpecFromRole(ColorRole::OnSurface)),
              .disabled =
                  state(colorSpecFromRole(ColorRole::Primary, 0.08F), colorSpecFromRole(ColorRole::OnSurface, 0.55F)),
              .selected = std::nullopt,
          }
      );
    }
    Radii radii;
    if (n == 1) {
      radii = Radii{r, r, r, r};
    } else if (i == 0) {
      radii = Radii{r, 0.0F, 0.0F, r};
    } else if (i == n - 1) {
      radii = Radii{0.0F, r, r, 0.0F};
    } else {
      radii = Radii{0.0F};
    }
    m_buttons[i]->setRadii(radii);
  }
}

void Segmented::applyOuterStyle() {
  Flex::setPadding(m_outerPadding);
  setFill(colorSpecFromRole(m_surfaceRole, m_surfaceOpacity));
  clearBorder();
  setRadius(Style::scaledRadiusMd(m_scale));
}

void Segmented::doLayout(Renderer& renderer) {
  Flex::doLayout(renderer);
  m_rovingNav.layoutOverlay(width(), height());
  if (m_activeUnderline != nullptr && m_selected < m_buttons.size() && m_buttons[m_selected] != nullptr) {
    Button* active = m_buttons[m_selected];
    const float targetX = active->x();
    const float targetWidth = active->width();
    const float underlineHeight = std::max(2.0F, Style::borderWidth * m_scale);
    m_activeUnderline->setSize(targetWidth, underlineHeight);
    m_activeUnderline->setPosition(m_activeUnderline->x(), std::max(0.0F, height() - underlineHeight));
    const bool selectionChanged = !m_underlineSelection || *m_underlineSelection != m_selected;
    const bool positionChanged = std::abs(targetX - m_underlineTargetX) > 0.5F;
    if (selectionChanged || positionChanged) {
      if (m_underlineAnimation != 0 && animationManager() != nullptr) {
        animationManager()->cancel(m_underlineAnimation);
      }
      const float from = m_activeUnderline->x();
      m_underlineSelection = m_selected;
      m_underlineTargetX = targetX;
      if (selectionChanged && animationManager() != nullptr && from != targetX) {
        m_underlineAnimation = animationManager()->animate(
            from, targetX, 120.0F, Easing::EaseOutCubic,
            [this](float x) {
              if (m_activeUnderline != nullptr) {
                m_activeUnderline->setPosition(x, std::max(0.0F, height() - m_activeUnderline->height()));
              }
            },
            {}, this
        );
      } else {
        m_activeUnderline->setPosition(targetX, std::max(0.0F, height() - underlineHeight));
      }
    }
  }
}

float Segmented::effectiveFontSize() const noexcept {
  return (m_fontSize > 0.0F ? m_fontSize : Style::fontSizeBody) * m_scale;
}
