#include "shell/window_inspector/window_inspector_panel.h"

#include "compositors/compositor_platform.h"
#include "i18n/i18n.h"
#include "render/core/image_resize.h"
#include "render/core/texture_manager.h"
#include "shell/panel/panel_manager.h"
#include "shell/window_inspector/window_inspector_target.h"
#include "system/app_identity.h"
#include "system/desktop_entry.h"
#include "ui/builders.h"
#include "util/string_utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>

WindowInspectorPanel::WindowInspectorPanel(CompositorPlatform& platform)
    : m_platform(platform), m_capture(platform.wayland()) {}

void WindowInspectorPanel::create() {
  m_buttons.clear();
  m_identityKeys.clear();
  auto column = ui::column({.out = &m_layout, .align = FlexAlign::Stretch, .gap = scaled(Style::spaceMd)});
  auto header = ui::row(
      {.out = &m_header,
       .align = FlexAlign::Center,
       .justify = FlexJustify::SpaceBetween,
       .wrap = true,
       .gap = scaled(Style::spaceSm)}
  );
  header->addChild(
      ui::label(
          {.text = i18n::tr("window-inspector.title"),
           .fontSize = scaled(Style::fontSizeTitle),
           .fontWeight = FontWeight::Bold,
           .color = colorSpecFromRole(ColorRole::Primary)}
      )
  );
  const auto button = [&](Button*& out, std::string text, ButtonVariant variant, std::function<void()> callback) {
    auto result = ui::button(
        {.out = &out,
         .text = std::move(text),
         .fontSize = scaled(Style::fontSizeCaption),
         .variant = variant,
         .surfaceOpacity = panelCardOpacity(),
         .minHeight = scaled(Style::controlHeightSm),
         .padding = scaled(Style::spaceSm),
         .radius = Style::scaledRadiusMd(contentScale()),
         .onClick = std::move(callback)}
    );
    m_buttons.push_back(out);
    return result;
  };
  auto headerActions = ui::row({.align = FlexAlign::Center, .gap = scaled(Style::spaceSm)});
  if (Style::closeButtonsEnabled()) {
    Button* dismiss = nullptr;
    headerActions->addChild(
        ui::button(
            {.out = &dismiss,
             .glyph = "close",
             .glyphSize = scaled(Style::fontSizeBody),
             .surfaceOpacity = panelCardOpacity(),
             .minWidth = scaled(Style::controlHeightSm),
             .minHeight = scaled(Style::controlHeightSm),
             .radius = Style::scaledRadiusMd(contentScale()),
             .onClick = [] { PanelManager::instance().closePanel(); }}
        )
    );
    m_buttons.push_back(dismiss);
  }
  header->addChild(std::move(headerActions));
  column->addChild(std::move(header));
  // Normally content fits without scrolling. This viewport is the safety net
  // when the shared panel host clamps us to a small work area.
  auto viewport = ui::scrollView(
      {.out = &m_viewport,
       .contentScale = contentScale(),
       .scrollbarVisible = true,
       .viewportPaddingH = 0.0F,
       .viewportPaddingV = 0.0F,
       .flexGrow = 1.0F,
       .configure = [](ScrollView& view) {
         view.clearFill();
         view.clearBorder();
         view.setReserveScrollbarGutter(true);
       }}
  );
  m_content = viewport->content();
  m_content->setDirection(FlexDirection::Vertical);
  m_content->setAlign(FlexAlign::Stretch);
  m_content->setGap(scaled(Style::spaceMd));
  auto main = ui::row({.out = &m_main, .align = FlexAlign::Start, .gap = scaled(Style::spaceMd)});
  auto visual = ui::column({.out = &m_previewColumn, .align = FlexAlign::Stretch, .gap = scaled(Style::spaceSm)});
  // The image owns its rounded alpha clip; the stage only centres it within
  // the preview column and contributes its intrinsic fitted height.
  auto stage = ui::row({.out = &m_previewStage, .align = FlexAlign::Center, .justify = FlexJustify::Center});
  stage->addChild(ui::image({.out = &m_preview, .fit = ImageFit::Contain, .configure = [this](Image& image) {
                               image.setRadius(Style::scaledRadiusSm(contentScale()));
                             }}));
  visual->addChild(std::move(stage));
  auto previewControls = ui::row({.align = FlexAlign::Center, .wrap = true, .gap = scaled(Style::spaceSm)});
  previewControls->addChild(
      ui::label(
          {.out = &m_previewStatus,
           .fontSize = scaled(Style::fontSizeCaption),
           .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
           .maxLines = 1,
           .flexGrow = 1.0F}
      )
  );
  previewControls->addChild(button(m_refresh, i18n::tr("window-inspector.pause-short"), ButtonVariant::Default, [this] {
    toggleLive();
  }));
  previewControls->addChild(
      ui::button(
          {.out = &m_snapshotRefresh,
           .glyph = "refresh",
           .glyphSize = scaled(Style::fontSizeBody),
           .surfaceOpacity = panelCardOpacity(),
           .tooltip = i18n::tr("window-inspector.refresh"),
           .minWidth = scaled(Style::controlHeightSm),
           .minHeight = scaled(Style::controlHeightSm),
           .radius = Style::scaledRadiusMd(contentScale()),
           .onClick = [this] { refreshSnapshot(true); }}
      )
  );
  m_buttons.push_back(m_snapshotRefresh);
  visual->addChild(std::move(previewControls));
  main->addChild(std::move(visual));

  auto console = ui::column({.out = &m_console, .align = FlexAlign::Stretch, .gap = scaled(Style::spaceSm)});
  console->addChild(
      ui::label(
          {.out = &m_application,
           .fontSize = scaled(Style::fontSizeTitle),
           .fontWeight = FontWeight::Bold,
           .color = colorSpecFromRole(ColorRole::OnSurface),
           .maxLines = 1}
      )
  );
  console->addChild(
      ui::label(
          {.out = &m_title,
           .fontSize = scaled(Style::fontSizeCaption),
           .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
           .maxLines = 2}
      )
  );
  auto identity = ui::column({.align = FlexAlign::Stretch, .gap = scaled(Style::spaceXs)});
  const auto identityRow = [&](std::string key, Label*& value) {
    auto row = ui::row({.align = FlexAlign::Center, .gap = scaled(Style::spaceSm)});
    Label* keyLabel = nullptr;
    row->addChild(
        ui::label(
            {.out = &keyLabel,
             .text = std::move(key),
             .fontSize = scaled(Style::fontSizeCaption),
             .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
             .maxLines = 1}
        )
    );
    m_identityKeys.push_back(keyLabel);
    row->addChild(
        ui::label(
            {.out = &value,
             .fontSize = scaled(Style::fontSizeCaption),
             .color = colorSpecFromRole(ColorRole::OnSurface),
             .maxLines = 1,
             .flexGrow = 1.0F}
        )
    );
    identity->addChild(std::move(row));
  };
  identityRow(i18n::tr("window-inspector.workspace"), m_workspaceValue);
  identityRow(i18n::tr("window-inspector.output"), m_outputValue);
  identityRow(i18n::tr("window-inspector.size"), m_sizeValue);
  console->addChild(std::move(identity));
  console->addChild(ui::separator({}));
  console->addChild(
      ui::label(
          {.out = &m_details,
           .fontSize = scaled(Style::fontSizeMini),
           .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
           .maxLines = 0}
      )
  );
  auto actions =
      ui::row({.out = &m_primaryActions, .align = FlexAlign::Center, .wrap = true, .gap = scaled(Style::spaceSm)});
  actions->addChild(button(m_focus, i18n::tr("window-inspector.focus"), ButtonVariant::Primary, [this] {
    act(false, false);
  }));
  actions->addChild(button(m_minimize, i18n::tr("common.minimize"), ButtonVariant::Default, [this] {
    act(true, false);
  }));
  actions->addChild(button(m_close, i18n::tr("window-inspector.close"), ButtonVariant::Destructive, [this] {
    act(false, true);
  }));
  auto states =
      ui::row({.out = &m_stateActions, .align = FlexAlign::Center, .wrap = true, .gap = scaled(Style::spaceSm)});
  states->addChild(button(m_float, i18n::tr("window-inspector.floating"), ButtonVariant::Default, [this] {
    runAction(InspectorWindowAction::Floating);
  }));
  states->addChild(button(m_pin, i18n::tr("window-inspector.pinned"), ButtonVariant::Default, [this] {
    runAction(InspectorWindowAction::Pinned);
  }));
  states->addChild(button(m_fullscreen, i18n::tr("window-inspector.fullscreen"), ButtonVariant::Default, [this] {
    runAction(InspectorWindowAction::Fullscreen);
  }));
  states->addChild(button(m_maximize, i18n::tr("window-inspector.maximized"), ButtonVariant::Default, [this] {
    runAction(InspectorWindowAction::Maximized);
  }));
  auto workspace =
      ui::row({.out = &m_workspaceRow, .align = FlexAlign::Center, .wrap = true, .gap = scaled(Style::spaceSm)});
  workspace->addChild(
      ui::select(
          {.out = &m_workspaceSelect,
           .placeholder = i18n::tr("window-inspector.workspace"),
           .fontSize = scaled(Style::fontSizeCaption),
           .controlHeight = scaled(Style::controlHeightSm),
           .surfaceOpacity = panelCardOpacity(),
           .onSelectionChanged = [this](std::size_t index, std::string_view) {
             if (index < m_workspaces.size())
               m_destinationId = m_workspaces[index].id;
             PanelManager::instance().requestUpdateOnly();
             PanelManager::instance().requestLayout();
           }}
      )
  );
  workspace->addChild(button(m_move, i18n::tr("window-inspector.move"), ButtonVariant::Default, [this] {
    runAction(InspectorWindowAction::MoveToWorkspace);
  }));
  console->addChild(std::move(workspace));
  console->addChild(
      ui::label(
          {.out = &m_actionStatus,
           .fontSize = scaled(Style::fontSizeCaption),
           .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
           .maxLines = 2,
           .visible = false,
           .participatesInLayout = false}
      )
  );
  auto danger = ui::row({.align = FlexAlign::Center, .gap = scaled(Style::spaceSm)});
  danger->addChild(button(m_kill, i18n::tr("window-inspector.force-quit"), ButtonVariant::Outline, [this] {
    m_killArmed = true;
    PanelManager::instance().requestUpdateOnly();
  }));
  m_kill->setGlyph("alert-triangle");
  m_kill->setTooltip(i18n::tr("window-inspector.kill-warning"));
  console->addChild(std::move(danger));
  auto confirmation = ui::column(
      {.out = &m_killConfirmation,
       .align = FlexAlign::Stretch,
       .gap = scaled(Style::spaceSm),
       .padding = scaled(Style::spaceSm),
       .visible = false,
       .participatesInLayout = false}
  );
  confirmation->setCardStyle(contentScale(), panelCardOpacity());
  confirmation->addChild(
      ui::label(
          {.text = i18n::tr("window-inspector.kill-warning"),
           .fontSize = scaled(Style::fontSizeCaption),
           .color = colorSpecFromRole(ColorRole::OnSurface),
           .maxLines = 0}
      )
  );
  auto confirmActions = ui::row({.wrap = true, .gap = scaled(Style::spaceSm)});
  Button* cancel = nullptr;
  confirmActions->addChild(button(cancel, i18n::tr("window-inspector.cancel"), ButtonVariant::Default, [this] {
    m_killArmed = false;
    PanelManager::instance().requestUpdateOnly();
  }));
  confirmActions->addChild(
      button(m_confirmKill, i18n::tr("window-inspector.force-quit"), ButtonVariant::Destructive, [this] {
        runAction(InspectorWindowAction::Kill);
      })
  );
  confirmation->addChild(std::move(confirmActions));
  console->addChild(std::move(confirmation));
  main->addChild(std::move(console));
  m_content->addChild(std::move(main));
  m_content->addChild(ui::separator({}));
  auto footer = ui::row(
      {.out = &m_footer,
       .align = FlexAlign::Center,
       .justify = FlexJustify::SpaceBetween,
       .wrap = true,
       .gap = scaled(Style::spaceMd)}
  );
  footer->addChild(std::move(actions));
  footer->addChild(std::move(states));
  m_content->addChild(std::move(footer));

  column->addChild(std::move(viewport));
  setRoot(std::move(column));
}

void WindowInspectorPanel::onOpen(std::string_view context) {
  m_capture.cancelInFlight();
  m_target.reset();
  m_pendingImage.reset();
  m_hasPreview = false;
  m_previewSourceWidth = 0;
  m_previewSourceHeight = 0;
  m_imageDirty = true;
  m_open = true;
  m_gone = false;
  m_live = true;
  m_killArmed = false;
  m_actionMessage.clear();
  m_destinationId.clear();
  m_workspaces.clear();
  // Select before the panel takes keyboard focus; titles are never identities.
  const auto selection = nlohmann::json::parse(context, nullptr, false);
  if (selection.is_object()
      && selection.contains("appId")
      && selection["appId"].is_string()
      && selection.contains("windowId")
      && selection["windowId"].is_string()
      && selection.contains("handle")
      && selection["handle"].is_number_unsigned()) {
    const auto appId = selection.value("appId", std::string{});
    const auto id = selection.value("windowId", std::string{});
    const auto handle = selection.value("handle", std::uintptr_t{0});
    const auto windows = appId.empty() ? m_platform.enrichedWindowsWithoutAppId()
                                       : m_platform.enrichedWindowsForApp(StringUtils::toLower(appId), "");
    for (const auto& window : windows) {
      if ((!id.empty() && window.exactIdentity && window.identifier == id)
          || (id.empty() && handle != 0 && reinterpret_cast<std::uintptr_t>(window.handle) == handle)) {
        m_target = window;
        break;
      }
    }
  }
  refreshSnapshot();
  m_refreshTimer.startRepeating(std::chrono::milliseconds(500), [] { PanelManager::instance().requestUpdateOnly(); });
}

std::optional<ToplevelInfo> WindowInspectorPanel::resolveTarget() const {
  if (!m_target || m_gone)
    return std::nullopt;
  const auto windows = m_target->appId.empty()
      ? m_platform.enrichedWindowsWithoutAppId()
      : m_platform.enrichedWindowsForApp(StringUtils::toLower(m_target->appId), "");
  for (const auto& window : windows)
    if (window_inspector::sameTarget(*m_target, window))
      return window;
  return std::nullopt;
}

void WindowInspectorPanel::refreshSnapshot(bool oneShot) {
  const auto window = resolveTarget();
  if (!window || m_capture.busy() || (!m_live && !oneShot))
    return;
  const auto minimized = m_platform.windowMinimizeState(*window);
  if (minimized && minimized->minimized)
    return;
  auto* handle = window->extHandle;
  if (handle == nullptr && window->exactIdentity)
    handle = m_platform.extHandleForCompositorWindowId(window->identifier);
  if (handle == nullptr || !m_capture.available())
    return;
  auto onFrame = [this](std::optional<ScreencopyImage> image, std::string error) {
    if (!m_open)
      return;
    if (image) {
      m_pendingImage = std::move(image);
      m_imageDirty = true;
    }
    if (!error.empty())
      m_live = false;
    PanelManager::instance().requestUpdateOnly();
  };
  if (oneShot)
    m_capture.capture(handle, 900, 450, std::move(onFrame));
  else
    m_capture.startLive(handle, 900, 450, std::chrono::milliseconds(125), std::move(onFrame));
}

void WindowInspectorPanel::toggleLive() {
  m_live = !m_live;
  if (m_live)
    refreshSnapshot();
  else
    m_capture.cancelInFlight();
  PanelManager::instance().requestUpdateOnly();
  PanelManager::instance().requestLayout();
}

void WindowInspectorPanel::syncWorkspaceChoices(const std::optional<InspectorWindowDetails>& details) {
  auto workspaces = m_platform.inspectorWorkspaces();
  if (workspaces == m_workspaces && !m_workspaces.empty())
    return;
  m_workspaces = std::move(workspaces);
  std::vector<std::string> labels;
  for (const auto& workspace : m_workspaces)
    labels.push_back(workspace.name + " · " + workspace.output);
  m_workspaceSelect->setOptions(std::move(labels));
  auto choice = std::ranges::find(m_workspaces, m_destinationId, &InspectorWorkspace::id);
  if (choice == m_workspaces.end() && details)
    choice = std::ranges::find(m_workspaces, details->workspaceId, &InspectorWorkspace::id);
  if (choice != m_workspaces.end()) {
    m_destinationId = choice->id;
    m_workspaceSelect->setSelectedIndexSilently(static_cast<std::size_t>(choice - m_workspaces.begin()));
  } else {
    m_destinationId.clear();
    m_workspaceSelect->clearSelection();
  }
}

void WindowInspectorPanel::doUpdate(Renderer& renderer) {
  if (!m_open || m_layout == nullptr)
    return;
  bool layoutChanged = false;
  const auto setLabel = [&](Label* label, const std::string& text) {
    if (label->text() != text) {
      label->setText(text);
      layoutChanged = true;
    }
  };
  const auto visibility = [&](Node* node, bool visible) {
    if (node->visible() != visible) {
      node->setVisible(visible);
      node->setParticipatesInLayout(visible);
      layoutChanged = true;
    }
  };
  const auto window = resolveTarget();
  if (!window && !m_gone) {
    m_gone = true;
    m_capture.cancelInFlight();
    m_pendingImage.reset();
    m_imageDirty = true;
    m_killArmed = false;
  }
  const auto state = window ? m_platform.windowMinimizeState(*window) : std::nullopt;
  if (state && state->minimized)
    m_capture.cancelInFlight();
  else if (m_live && window && !m_capture.busy())
    refreshSnapshot();
  if (m_imageDirty) {
    if (m_pendingImage && window) {
      if (m_previewSourceWidth != m_pendingImage->width || m_previewSourceHeight != m_pendingImage->height) {
        m_previewSourceWidth = m_pendingImage->width;
        m_previewSourceHeight = m_pendingImage->height;
        layoutChanged = true;
      }
      m_hasPreview = m_preview->setSourceRaw(
          renderer, m_pendingImage->rgba.data(), m_pendingImage->rgba.size(), m_pendingImage->width,
          m_pendingImage->height, m_pendingImage->width * 4, PixmapFormat::RGBA, false
      );
    } else if (!window) {
      m_preview->clear(renderer);
      m_hasPreview = false;
    }
    m_pendingImage.reset();
    m_imageDirty = false;
  }
  visibility(m_preview, m_hasPreview);
  setLabel(
      m_previewStatus,
      i18n::tr(
          !m_hasPreview                    ? "window-inspector.unavailable"
              : m_live && m_capture.busy() ? "window-inspector.live-short"
                                           : "window-inspector.snapshot-short"
      )
  );
  m_previewStatus->setTooltip(
      i18n::tr(
          !m_hasPreview                    ? "window-inspector.preview-unavailable"
              : m_live && m_capture.busy() ? "window-inspector.live"
                                           : "window-inspector.paused"
      )
  );
  const auto appId = window ? window->appId : std::string{};
  if (appId != m_applicationId || m_desktopEntriesVersion != desktopEntriesVersion()) {
    m_applicationId = appId;
    m_desktopEntriesVersion = desktopEntriesVersion();
    const auto entry = app_identity::findDesktopEntry(appId, desktopEntries());
    m_applicationName = entry && !entry->name.empty() ? entry->name : appId;
  }
  const auto title = window ? StringUtils::windowTitleSingleLine(window->title) : std::string{};
  setLabel(
      m_application,
      window ? (m_applicationName.empty() ? i18n::tr("window-inspector.unavailable") : m_applicationName)
             : i18n::tr("window-inspector.window-gone")
  );
  setLabel(m_title, title);
  m_application->setTooltip(m_applicationName);
  m_title->setTooltip(title);
  visibility(m_title, !title.empty() && StringUtils::toLower(title) != StringUtils::toLower(m_applicationName));
  const auto details =
      window && window->exactIdentity ? m_platform.inspectorWindowDetails(window->identifier) : std::nullopt;
  const auto unknown = i18n::tr("window-inspector.unavailable");
  const auto value = [&](const auto& optional) { return optional ? std::to_string(*optional) : unknown; };
  const auto flag = [&](std::optional<bool> optional) {
    return optional ? i18n::tr(*optional ? "window-inspector.yes" : "window-inspector.no") : unknown;
  };
  std::string text;
  const auto line = [&](std::string label, std::string content) {
    if (!text.empty())
      text += "\n";
    text += label + ": " + (content.empty() ? unknown : content);
  };
  line(i18n::tr("window-inspector.application-id"), window ? window->appId : unknown);
  line(i18n::tr("window-inspector.identity"), window && window->exactIdentity ? window->identifier : unknown);
  std::string workspace = unknown;
  std::string output = unknown;
  std::string size = unknown;
  if (details) {
    workspace = details->workspaceId;
    for (const auto& candidate : m_platform.inspectorWorkspaces())
      if (candidate.id == workspace) {
        workspace = candidate.name;
        break;
      }
    output = details->output.empty() ? unknown : details->output;
    size = value(details->width) + " × " + value(details->height);
    line(i18n::tr("window-inspector.position"), value(details->x) + ", " + value(details->y));
    line(i18n::tr("window-inspector.pid"), value(details->pid));
    line(i18n::tr("window-inspector.xwayland"), flag(details->xwayland));
    line(i18n::tr("window-inspector.minimized"), state ? flag(state->minimized) : unknown);
    line(i18n::tr("window-inspector.visible"), flag(details->visible));
    if (!details->scratchpad.empty())
      line(i18n::tr("window-inspector.scratchpad"), details->scratchpad);
    if (!details->parent.empty())
      line(i18n::tr("window-inspector.parent"), details->parent);
    if (!details->region.empty())
      line(i18n::tr("window-inspector.region"), details->region);
    if (details->column && *details->column >= 0)
      line(i18n::tr("window-inspector.column-row"), value(details->column) + ", " + value(details->row));
    if (details->tabbed.value_or(false)) {
      line(i18n::tr("window-inspector.tab"), value(details->tabIndex));
      line(i18n::tr("window-inspector.tab-hidden"), flag(details->tabHidden));
    }
  } else {
    for (const auto& assignment : m_platform.workspaceWindowAssignments())
      if (window && assignment.windowId == window->identifier) {
        workspace = assignment.workspaceKey;
        line(i18n::tr("window-inspector.position"), std::to_string(assignment.x) + ", " + std::to_string(assignment.y));
        break;
      }
  }
  setLabel(m_workspaceValue, workspace);
  setLabel(m_outputValue, output);
  setLabel(m_sizeValue, size);
  m_workspaceValue->setTooltip(workspace);
  m_outputValue->setTooltip(output);
  m_sizeValue->setTooltip(size);
  setLabel(m_details, text);
  const auto capabilities = window && window->exactIdentity ? m_platform.inspectorWindowCapabilities(window->identifier)
                                                            : InspectorWindowCapabilities{};
  const auto control = [&](Button* button, bool enabled, std::optional<bool> selected) {
    button->setEnabled(enabled);
    button->setSelected(selected.value_or(false));
    button->setTooltip(
        enabled ? std::string{}
                : i18n::tr(
                      details && selected.has_value() && !details->active && button != m_float
                          ? "window-inspector.focus-hint"
                          : "window-inspector.unavailable"
                  )
    );
  };
  control(m_float, capabilities.floating, details ? details->floating : std::nullopt);
  control(m_pin, capabilities.pinned, details ? details->pinned : std::nullopt);
  control(m_fullscreen, capabilities.fullscreen, details ? details->fullscreen : std::nullopt);
  control(m_maximize, capabilities.maximized, details ? details->maximized : std::nullopt);
  m_focus->setEnabled(window.has_value());
  m_close->setEnabled(window.has_value());
  m_minimize->setEnabled(state && state->minimizable && !state->minimized);
  const bool canPreview = window
      && m_capture.available()
      && (window->extHandle != nullptr
          || (window->exactIdentity && m_platform.extHandleForCompositorWindowId(window->identifier) != nullptr));
  m_refresh->setEnabled(canPreview && !(state && state->minimized));
  m_refresh->setText(i18n::tr(m_live ? "window-inspector.pause-short" : "window-inspector.resume-short"));
  m_snapshotRefresh->setEnabled(canPreview && !m_live && !m_capture.busy() && !(state && state->minimized));
  syncWorkspaceChoices(details);
  m_workspaceSelect->setEnabled(capabilities.moveToWorkspace);
  m_move->setEnabled(
      capabilities.moveToWorkspace && !m_destinationId.empty() && (!details || m_destinationId != details->workspaceId)
  );
  m_kill->setEnabled(capabilities.kill);
  m_confirmKill->setEnabled(capabilities.kill && m_killArmed);
  m_move->setTooltip(capabilities.moveToWorkspace ? std::string{} : i18n::tr("window-inspector.focus-hint"));
  setLabel(m_actionStatus, m_actionMessage);
  visibility(m_actionStatus, !m_actionMessage.empty());
  visibility(m_killConfirmation, m_killArmed);
  if (layoutChanged)
    PanelManager::instance().requestLayout();
}

void WindowInspectorPanel::runAction(InspectorWindowAction action) {
  const auto window = resolveTarget();
  if (!window || !window->exactIdentity || (action == InspectorWindowAction::Kill && !m_killArmed))
    return;
  const bool success = m_platform.runInspectorWindowAction(window->identifier, action, m_destinationId);
  m_killArmed = false;
  m_actionMessage = i18n::tr(success ? "window-inspector.action-sent" : "window-inspector.action-failed");
  if (success && action == InspectorWindowAction::Kill) {
    PanelManager::instance().closePanel();
    return;
  }
  PanelManager::instance().requestUpdateOnly();
}

void WindowInspectorPanel::act(bool minimize, bool close) {
  const auto window = resolveTarget();
  if (!window)
    return;
  if (minimize) {
    if (!m_platform.minimizeToplevelInfo(*window))
      return;
  } else if (close)
    m_platform.closeToplevelInfo(*window);
  else {
    PanelManager::instance().closePanel(false);
    m_platform.activateToplevelInfo(*window);
    return;
  }
  PanelManager::instance().closePanel();
}

void WindowInspectorPanel::prepareContentLayout(Renderer& renderer, float width) {
  // Reset the last assigned height before intrinsic measurement. Use the same
  // inset/gutter budget as the scroll view's final layout, including on overflow.
  m_viewport->setSize(width, 0.0F);
  width = std::max(1.0F, m_viewport->contentViewportWidth(true));
  const float gap = scaled(Style::spaceMd);
  const bool sideBySide = width >= scaled(660.0F);
  const float consoleWidth = sideBySide ? std::max(scaled(300.0F), width * 0.42F) : width;
  const float visualWidth = sideBySide ? width - consoleWidth - gap : width;
  m_main->setDirection(sideBySide ? FlexDirection::Horizontal : FlexDirection::Vertical);
  m_main->setSize(width, 0.0F);
  m_console->setSize(consoleWidth, 0.0F);
  m_previewColumn->setSize(visualWidth, 0.0F);
  m_application->setMaxWidth(consoleWidth);
  m_title->setMaxWidth(consoleWidth);
  m_actionStatus->setMaxWidth(consoleWidth);
  const float keyWidth = std::min(scaled(72.0F), consoleWidth * 0.32F);
  for (auto* key : m_identityKeys) {
    key->setMinWidth(keyWidth);
    key->setMaxWidth(keyWidth);
  }
  for (auto* value : {m_workspaceValue, m_outputValue, m_sizeValue})
    value->setMaxWidth(std::max(1.0F, consoleWidth - keyWidth - scaled(Style::spaceSm)));
  const float moveWidth = m_move->measure(renderer, {}).width;
  const auto destination = m_workspaceSelect->selectedText();
  const auto destinationLabel = destination.empty() ? i18n::tr("window-inspector.workspace") : std::string(destination);
  const float destinationWidth =
      renderer.measureText(destinationLabel, scaled(Style::fontSizeCaption)).width + scaled(48.0F);
  const float availableDestinationWidth = std::max(scaled(64.0F), consoleWidth - moveWidth - scaled(Style::spaceSm));
  m_workspaceSelect->setSize(
      std::min(availableDestinationWidth, std::clamp(destinationWidth, scaled(112.0F), scaled(220.0F))),
      scaled(Style::controlHeightSm)
  );
  // Fit both axes without distorting the capture. Only the maximum is bounded;
  // a shallow capture contributes a shallow preview, not a fixed-height stage.
  const float maxPreviewHeight = scaled(sideBySide ? 320.0F : 240.0F);
  const auto preview = fitImageWithin(
      m_previewSourceWidth, m_previewSourceHeight, std::max(1, static_cast<int>(std::floor(visualWidth))),
      std::max(1, static_cast<int>(std::floor(maxPreviewHeight)))
  );
  m_previewStage->setSize(visualWidth, static_cast<float>(preview.height));
  m_preview->setSize(static_cast<float>(preview.width), static_cast<float>(preview.height));
  // The footer wraps whole groups; each group also needs a known width to
  // measure its own wrapped button rows before arrangement, not afterward.
  for (auto* group : {m_primaryActions, m_stateActions}) {
    group->setSize(0.0F, 0.0F);
    const float naturalWidth = group->measure(renderer, {}).width;
    group->setSize(std::min(width, naturalWidth), 0.0F);
  }
  m_footer->setSize(width, 0.0F);
  m_previewStatus->setMaxWidth(std::max(scaled(32.0F), visualWidth - scaled(140.0F)));
  m_details->setMaxWidth(consoleWidth);
  m_content->setSize(width, 0.0F);
}

float WindowInspectorPanel::measureContentHeight(Renderer& renderer, float contentWidth) {
  if (m_layout == nullptr)
    return 0.0F;
  prepareContentLayout(renderer, contentWidth);
  m_header->setSize(contentWidth, 0.0F);
  m_layout->setSize(contentWidth, 0.0F);
  LayoutConstraints constraints;
  constraints.setExactWidth(contentWidth);
  // Measure the actual tree, including the viewport's own insets, every section
  // gap, and the footer's wrapped rows. PanelManager adds shell decoration padding
  // and caps/animates the resulting height against the available surface slot.
  return m_layout->measure(renderer, constraints).height;
}

bool WindowInspectorPanel::dismissTransientUi() {
  if (!m_killArmed)
    return false;
  m_killArmed = false;
  PanelManager::instance().requestUpdateOnly();
  return true;
}

void WindowInspectorPanel::doLayout(Renderer& renderer, float width, float height) {
  if (m_layout == nullptr)
    return;
  prepareContentLayout(renderer, width);
  m_header->setSize(width, 0.0F);
  // The flex viewport receives remaining height only at arrangement time.
  // At intrinsic measurement time its height is unconstrained.
  m_layout->setSize(width, height);
  m_layout->layout(renderer);
  for (auto* button : m_buttons)
    button->updateInputArea();
}

void WindowInspectorPanel::onPanelCardOpacityChanged(float opacity) {
  if (m_killConfirmation)
    m_killConfirmation->setCardStyle(contentScale(), opacity);
  if (m_workspaceSelect)
    m_workspaceSelect->setSurfaceOpacity(opacity);
  for (auto* button : m_buttons)
    button->setSurfaceOpacity(opacity);
}

void WindowInspectorPanel::onClose() {
  m_open = false;
  m_refreshTimer.stop();
  m_capture.cancelInFlight();
  m_target.reset();
  m_pendingImage.reset();
  m_buttons.clear();
  m_identityKeys.clear();
  m_layout = nullptr;
  m_killConfirmation = nullptr;
  m_workspaceSelect = nullptr;
  clearReleasedRoot();
}
