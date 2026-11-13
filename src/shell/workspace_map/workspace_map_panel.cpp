#include "shell/workspace_map/workspace_map_panel.h"

#include "compositors/compositor_platform.h"
#include "config/config_service.h"
#include "core/deferred_call.h"
#include "cursor-shape-v1-client-protocol.h"
#include "i18n/i18n.h"
#include "render/core/texture_manager.h"
#include "shell/panel/panel_manager.h"
#include "system/app_identity.h"
#include "ui/builders.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <unordered_set>
#include <utility>

WorkspaceMapPanel::WorkspaceMapPanel(CompositorPlatform& platform, ConfigService* config, bool expanded)
    : m_platform(platform), m_config(config), m_expanded(expanded), m_capture(platform.wayland()) {}

std::string WorkspaceMapPanel::outputStateKey(std::string_view output, std::string_view dimension) {
  // State identifiers allow only lowercase letters, digits and underscores.
  // Byte encoding preserves distinct connector names without lossy sanitizing.
  constexpr char digits[] = "0123456789abcdef";
  std::string key = "output_";
  for (const unsigned char value : output) {
    key += digits[value >> 4];
    key += digits[value & 15];
  }
  return key + "_" + std::string(dimension);
}

std::pair<float, float> WorkspaceMapPanel::preferredOutputSize() const {
  // PanelManager sets this context before asking for the surface size. onOpen
  // runs later, so using m_output alone would size for the previous monitor.
  const std::string outputName(pendingOpenContext().empty() ? std::string_view(m_output) : pendingOpenContext());
  if (const auto found = m_outputSizes.find(outputName); found != m_outputSizes.end())
    return found->second;
  std::pair<float, float> size{960.0F, 640.0F};
  for (const auto& output : m_platform.outputs()) {
    if (output.connectorName != outputName)
      continue;
    if (m_expanded) {
      size = {
          static_cast<float>(output.effectiveLogicalWidth()) * 0.9F / contentScale(),
          static_cast<float>(output.effectiveLogicalHeight()) * 0.9F / contentScale()
      };
      break;
    }
    std::unordered_set<std::string> occupied;
    for (const auto& window : m_platform.workspaceWindowAssignments(output.output)) {
      if (const auto details = m_platform.inspectorWindowDetails(window.windowId);
          details && includesWindow(*details, outputName))
        occupied.insert(details->workspaceId);
    }
    const auto visible = m_platform.workspaces(output.output);
    const LayoutRect bounds{
        0, 0, static_cast<float>(output.effectiveLogicalWidth()), static_cast<float>(output.effectiveLogicalHeight())
    };
    const bool portrait = bounds.height > bounds.width;
    float contentHeight = 0;
    std::size_t count = 0;
    for (const auto& workspace : m_platform.inspectorWorkspaces()) {
      if (workspace.output != outputName)
        continue;
      const bool active = std::ranges::any_of(visible, [&](const Workspace& current) {
        return current.id == workspace.id && current.active;
      });
      const float extent = occupied.contains(workspace.id) || active ? monitorMapHeight(bounds, 1) : 28;
      contentHeight = portrait ? std::max(contentHeight, extent) : contentHeight + extent;
      ++count;
    }
    if (count > 1 && !portrait)
      contentHeight += static_cast<float>(count - 1) * Style::spaceXs;
    size = initialPanelSize(bounds.width / contentScale(), bounds.height / contentScale(), contentHeight + 100);
    break;
  }
  if (m_config && !outputName.empty()) {
    const auto restore = [&](std::string_view suffix, float fallback) {
      const auto saved = m_config->stateString(
          m_expanded ? "workspace_map_expanded" : "workspace_map", outputStateKey(outputName, suffix)
      );
      if (!saved)
        return fallback;
      int value = 0;
      const auto [end, error] = std::from_chars(saved->data(), saved->data() + saved->size(), value);
      return error == std::errc{} && end == saved->data() + saved->size() && value >= 280 && value <= 16384
          ? static_cast<float>(value)
          : fallback;
    };
    size.first = restore("width", size.first);
    size.second = restore("height", size.second);
  }
  return size;
}

std::pair<float, float>
WorkspaceMapPanel::initialPanelSize(float outputWidth, float outputHeight, float contentHeight) {
  if (outputWidth <= 0 || outputHeight <= 0)
    return {960, 640};
  const float aspect = outputWidth / outputHeight;
  float height = std::max(320.0F, contentHeight);
  float width = height * aspect;
  if (width < 400) {
    width = 400;
    height = width / aspect;
  }
  const float fit = std::min({1.0F, outputWidth * 0.8F / width, outputHeight * 0.8F / height});
  return {width * fit, height * fit};
}

float WorkspaceMapPanel::preferredWidth() const { return scaled(preferredOutputSize().first); }
float WorkspaceMapPanel::preferredHeight() const { return scaled(preferredOutputSize().second); }

float WorkspaceMapPanel::monitorMapHeight(const LayoutRect& output, float scale) {
  // A height-based miniature makes portrait windows tiny. Give portrait
  // monitors a readable 240px width while keeping one uniform spatial scale.
  if (output.width > 0 && output.height > output.width)
    return 240.0F * scale * output.height / output.width + 6.0F * scale;
  return 148.0F * scale;
}

PanelPlacement WorkspaceMapPanel::panelPlacement() const noexcept {
  return m_expanded ? PanelPlacement::Floating
      : m_config    ? m_config->config().shell.panel.workspaceMapPlacement
                    : PanelPlacement::Attached;
}

void WorkspaceMapPanel::onResizeFinished(float width, float height) {
  if (m_output.empty())
    return;
  const std::pair size{width / contentScale(), height / contentScale()};
  m_outputSizes[m_output] = size;
  if (m_config) {
    m_config->setStateString(
        m_expanded ? "workspace_map_expanded" : "workspace_map", outputStateKey(m_output, "width"),
        std::to_string(static_cast<int>(std::lround(size.first)))
    );
    m_config->setStateString(
        m_expanded ? "workspace_map_expanded" : "workspace_map", outputStateKey(m_output, "height"),
        std::to_string(static_cast<int>(std::lround(size.second)))
    );
  }
}

void WorkspaceMapPanel::dismiss() {
  if (m_closeWindow)
    m_closeWindow();
  else
    PanelManager::instance().closePanel(false);
}

void WorkspaceMapPanel::requestRefresh() {
  if (m_refreshWindow)
    m_refreshWindow();
  else if (auto* manager = PanelManager::current())
    manager->refreshPanel("workspace-map");
}

void WorkspaceMapPanel::bindPan(InputArea* area) {
  area->setCursorShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB);
  area->setOnPress([this](const InputArea::PointerData& data) {
    if (data.button != BTN_LEFT)
      return;
    if (data.pressed) {
      m_pan = {
          .active = true,
          .x = data.sceneX,
          .y = data.sceneY,
          .horizontal = m_horizontal->scrollOffset(),
          .vertical = m_scroll->scrollOffset()
      };
    } else {
      m_pan.active = false;
      if (!m_pendingCloses.empty() || m_livePreviews)
        requestRefresh();
    }
  });
  area->setOnCancel([this] {
    m_pan = {};
    if (m_snapshotOpen && m_livePreviews)
      requestRefresh();
  });
  area->setOnMotion([this, area](const InputArea::PointerData& data) {
    if (!m_pan.active)
      return;
    float x = 0, y = 0;
    Node::mapToScene(area, data.localX, data.localY, x, y);
    const float dx = x - m_pan.x, dy = y - m_pan.y;
    if (!m_pan.moved && std::hypot(dx, dy) < scaled(5.0F))
      return;
    m_pan.moved = true;
    m_horizontal->setScrollOffset(m_pan.horizontal - dx);
    m_scroll->setScrollOffset(m_pan.vertical - dy);
  });
}

void WorkspaceMapPanel::onClose() {
  m_pan = {};
  m_snapshotOpen = false;
  m_previewTimer.stop();
  m_nextPreviewCapture = {};
  ++m_snapshotGeneration;
  m_capture.cancelInFlight();
  m_snapshots.clear();
  m_snapshotBytes = 0;
}

bool WorkspaceMapPanel::storeSnapshot(const std::string& id, ScreencopyImage image) {
  auto& snapshot = m_snapshots[id];
  const auto previousBytes = snapshot.image ? snapshot.image->rgba.size() : 0;
  const auto retainedBytes = m_snapshotBytes - previousBytes;
  if (image.rgba.size() > kSnapshotBudget - retainedBytes)
    return false;
  m_snapshotBytes = retainedBytes + image.rgba.size();
  snapshot.image = std::move(image);
  return true;
}

std::chrono::milliseconds WorkspaceMapPanel::previewDelay(std::chrono::milliseconds elapsed, bool initialFill) {
  if (initialFill)
    return std::chrono::milliseconds::zero();
  return std::max(std::chrono::milliseconds(250), elapsed * 3);
}

void WorkspaceMapPanel::schedulePreview(std::chrono::milliseconds delay) {
  if (m_snapshotOpen && (m_livePreviews || hasVisibleSnapshotGap()) && !m_previewSuspended) {
    m_nextPreviewCapture = std::chrono::steady_clock::now() + delay;
    m_previewTimer.start(delay, [this] { captureNextSnapshot(); });
  }
}

bool WorkspaceMapPanel::hasVisibleSnapshotGap() const {
  if (m_scroll == nullptr)
    return false;
  for (const auto& workspace : m_workspaces) {
    for (const auto& tile : workspace.tiles) {
      const auto snapshot = m_snapshots.find(tile.id);
      if (snapshotVisible(tile) && (snapshot == m_snapshots.end() || !snapshot->second.image))
        return true;
    }
  }
  return false;
}

bool WorkspaceMapPanel::previewReady(std::chrono::steady_clock::time_point now) const {
  // Timer::active() stays true inside its own fired callback. The deadline,
  // rather than the timer's lifetime, determines when another capture is due.
  return m_snapshotOpen
      && (m_livePreviews || hasVisibleSnapshotGap())
      && !m_previewSuspended
      && !m_pan.active
      && !m_snapshotLayout
      && !m_rebuildPending
      && !m_capture.busy()
      && now >= m_nextPreviewCapture;
}

void WorkspaceMapPanel::setPreviewSuspended(bool suspended) {
  if (m_previewSuspended == suspended)
    return;
  m_previewSuspended = suspended;
  ++m_snapshotGeneration;
  m_previewTimer.stop();
  m_nextPreviewCapture = {};
  m_capture.cancelInFlight();
  if (!suspended && m_snapshotOpen)
    requestRefresh();
}

void WorkspaceMapPanel::toggleLivePreviews() {
  m_livePreviews = !m_livePreviews;
  m_previewTimer.stop();
  m_nextPreviewCapture = {};
  if (m_livePreviews) {
    requestRefresh();
  } else {
    // Let an in-flight capture finish, then seed any other visible tiles once.
    // Otherwise pausing before the first frame arrives leaves the map blank.
    captureNextSnapshot();
    requestRefresh();
  }
}

bool WorkspaceMapPanel::snapshotVisible(const Tile& tile) const {
  if (!tile.area || tile.frame.width < scaled(32.0F) || tile.frame.height < scaled(56.0F))
    return false;
  float left = 0, top = 0, right = 0, bottom = 0;
  float viewLeft = 0, viewTop = 0, viewRight = 0, viewBottom = 0;
  Node::transformedBounds(tile.area, left, top, right, bottom);
  Node::transformedBounds(m_scroll->viewportArea(), viewLeft, viewTop, viewRight, viewBottom);
  return left < viewRight && right > viewLeft && top < viewBottom && bottom > viewTop;
}

void WorkspaceMapPanel::captureNextSnapshot() {
  // A single capture slot and a map-wide rate limit, never one stream per tile.
  constexpr std::size_t budget = kSnapshotBudget;
  constexpr std::size_t maxThumbnailBytes = 320U * 200U * 4U;
  const auto now = std::chrono::steady_clock::now();
  if (!previewReady(now) || !m_capture.available())
    return;
  Tile* candidate = nullptr;
  ext_foreign_toplevel_handle_v1* candidateHandle = nullptr;
  auto retryAt = std::chrono::steady_clock::time_point::max();
  for (auto& workspace : m_workspaces) {
    for (auto& tile : workspace.tiles) {
      if (!snapshotVisible(tile))
        continue;
      auto& snapshot = m_snapshots[tile.id];
      if (!m_livePreviews && snapshot.image)
        continue;
      if (!snapshot.image && m_snapshotBytes + maxThumbnailBytes > budget)
        continue;
      if (snapshot.retryAt > now) {
        retryAt = std::min(retryAt, snapshot.retryAt);
        continue;
      }
      auto* handle =
          m_platform.inspectorWindowDetails(tile.id) ? m_platform.extHandleForCompositorWindowId(tile.id) : nullptr;
      if (!handle) {
        snapshot.retryAt = now + std::chrono::seconds(5);
        retryAt = std::min(retryAt, snapshot.retryAt);
        continue;
      }
      if (!candidate || snapshot.lastCaptured < m_snapshots[candidate->id].lastCaptured) {
        candidate = &tile;
        candidateHandle = handle;
      }
    }
  }
  if (!candidate) {
    if (retryAt != std::chrono::steady_clock::time_point::max())
      schedulePreview(std::chrono::ceil<std::chrono::milliseconds>(retryAt - now));
    return;
  }
  auto& snapshot = m_snapshots[candidate->id];
  snapshot.lastCaptured = now;
  const auto generation = m_snapshotGeneration;
  m_capture.capture(
      candidateHandle, 320, 200,
      [this, id = candidate->id, generation, started = now](std::optional<ScreencopyImage> image, std::string) {
        if (!m_snapshotOpen || generation != m_snapshotGeneration)
          return;
        bool changed = false;
        if (image && m_platform.inspectorWindowDetails(id)) {
          auto& cached = m_snapshots[id];
          const bool identical = cached.image
              && cached.image->width == image->width
              && cached.image->height == image->height
              && cached.image->rgba == image->rgba;
          if (identical) {
            cached.retryAt = {};
          } else if (storeSnapshot(id, std::move(*image))) {
            cached.retryAt = {};
            changed = true;
            for (auto& workspace : m_workspaces)
              for (auto& tile : workspace.tiles)
                if (tile.id == id)
                  tile.previewUploadAttempted = false;
          } else {
            cached.retryAt = std::chrono::steady_clock::now() + std::chrono::seconds(5);
          }
        } else {
          m_snapshots[id].retryAt = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }
        // Keep a 250ms idle gap between captures. Slow capture paths back off further.
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        schedulePreview(previewDelay(elapsed, hasVisibleSnapshotGap()));
        if (changed)
          requestRefresh();
      }
  );
}

void WorkspaceMapPanel::closeWindow(const std::string& id) {
  if (m_pan.moved || !m_platform.inspectorWindowDetails(id))
    return;
  m_platform.closeToplevelInfo(ToplevelInfo{.identifier = id, .exactIdentity = true});
  if (std::ranges::find(m_pendingCloses, id) == m_pendingCloses.end())
    m_pendingCloses.push_back(id);
}

void WorkspaceMapPanel::doUpdate(Renderer&) {
  if (m_pendingCloses.empty() || m_pan.active)
    return;
  bool changed = false;
  std::erase_if(m_pendingCloses, [this, &changed](const std::string& id) {
    if (m_platform.inspectorWindowDetails(id))
      return false; // A normal close may wait for the application's confirmation dialog.
    for (auto& workspace : m_workspaces)
      changed |= std::erase_if(workspace.tiles, [&](const Tile& tile) { return tile.id == id; }) > 0;
    if (const auto snapshot = m_snapshots.find(id); snapshot != m_snapshots.end()) {
      if (snapshot->second.image)
        m_snapshotBytes -= snapshot->second.image->rgba.size();
      m_snapshots.erase(snapshot);
    }
    return true;
  });
  if (changed) {
    m_preservePan = true;
    m_rebuildPending = true;
    root()->markLayoutDirty();
  }
}

void WorkspaceMapPanel::onOpen(std::string_view context) {
  m_previewTimer.stop();
  m_nextPreviewCapture = {};
  m_capture.cancelInFlight();
  ++m_snapshotGeneration;
  m_previewSuspended = false;
  m_livePreviews = true;
  m_snapshotOpen = true;
  m_snapshots.clear();
  m_snapshotBytes = 0;
  m_output = context;
  m_pan = {};
  m_pendingCloses.clear();
  m_preservePan = false;
  m_workspaces.clear();
  m_outputBounds = {};
  m_rebuildPending = true;
  // Geometry and identity are optional backend metadata. Unsupported compositors
  // get an empty state; the UI never talks directly to Umbriel IPC.
  for (const auto& output : m_platform.outputs()) {
    if (output.connectorName != m_output)
      continue;
    m_outputBounds = {
        static_cast<float>(output.logicalX), static_cast<float>(output.logicalY),
        static_cast<float>(output.effectiveLogicalWidth()), static_cast<float>(output.effectiveLogicalHeight())
    };
    if (const auto area = m_platform.outputWorkArea(output.output)) {
      m_outputBounds = {
          static_cast<float>(area->x), static_cast<float>(area->y), static_cast<float>(area->width),
          static_cast<float>(area->height)
      };
    }
    // Keep the compositor's workspace order, including empty workspaces.
    for (const auto& workspace : m_platform.inspectorWorkspaces()) {
      if (workspace.output == m_output)
        ensureWorkspace(workspace.id, workspace.name);
    }
    const auto focused = m_platform.focusedCompositorWindowId();
    const auto& entries = desktopEntries();
    for (const auto& window : m_platform.workspaceWindowAssignments(output.output)) {
      const auto details = m_platform.inspectorWindowDetails(window.windowId);
      if (window.windowId.empty() || !details || !includesWindow(*details, m_output))
        continue;
      const auto app = app_identity::resolveRunningDesktopEntry(window.appId, entries);
      const std::string appName = app.name.empty() ? i18n::tr("window-inspector.application") : app.name;
      std::string iconPath = m_iconResolver.resolve(app.icon.empty() ? window.appId : app.icon);
      if (iconPath.empty())
        iconPath = m_iconResolver.resolve("application-x-executable");
      ensureWorkspace(details->workspaceId)
          .tiles.push_back({
              .id = window.windowId,
              .title = window.title.empty() ? appName : window.title,
              .appName = appName,
              .iconPath = std::move(iconPath),
              .x = static_cast<double>(*details->x),
              .y = static_cast<double>(*details->y),
              .width = static_cast<double>(*details->width),
              .height = static_cast<double>(*details->height),
              .focused = focused == window.windowId,
          });
    }
    const auto outputWorkspaces = m_platform.workspaces(output.output);
    for (auto& workspace : m_workspaces) {
      workspace.visible = std::ranges::any_of(outputWorkspaces, [&](const Workspace& current) {
        return current.active && current.id == workspace.id;
      });
    }
    break;
  }
  if (root())
    root()->markLayoutDirty();
}

void WorkspaceMapPanel::create() {
  auto root = ui::node({});
  auto header = ui::row({.out = &m_header, .align = FlexAlign::Center, .justify = FlexJustify::SpaceBetween});
  header->addChild(
      ui::label({
          .out = &m_heading,
          .text = i18n::tr("workspace-map.title") + " · " + m_output,
          .fontSize = scaled(Style::fontSizeTitle),
          .fontWeight = FontWeight::Bold,
          .color = colorSpecFromRole(ColorRole::Primary),
          .maxLines = 1,
          .flexGrow = 1.0F,
      })
  );
  if (!m_expanded) {
    header->addChild(
        ui::button({
            .glyph = "arrows-maximize",
            .glyphSize = scaled(Style::fontSizeBody),
            .tooltip = i18n::tr("workspace-map.expand"),
            .minWidth = scaled(Style::controlHeightSm),
            .minHeight = scaled(Style::controlHeightSm),
            .onClick = [this] {
              const std::string outputName = m_output;
              DeferredCall::callLater([this, outputName] {
                if (m_expand)
                  m_expand(outputName);
              });
            },
        })
    );
  }
  header->addChild(
      ui::button({
          .out = &m_previewToggle,
          .glyph = m_livePreviews ? "media-pause" : "media-play",
          .glyphSize = scaled(Style::fontSizeBody),
          .tooltip = i18n::tr(m_livePreviews ? "window-inspector.pause-short" : "window-inspector.resume-short"),
          .minWidth = scaled(Style::controlHeightSm),
          .minHeight = scaled(Style::controlHeightSm),
          .onClick = [this] { toggleLivePreviews(); },
      })
  );
  header->addChild(
      ui::button({
          .glyph = "close",
          .glyphSize = scaled(Style::fontSizeBody),
          .minWidth = scaled(Style::controlHeightSm),
          .minHeight = scaled(Style::controlHeightSm),
          .onClick = [this] { dismiss(); },
      })
  );
  root->addChild(std::move(header));
  root->addChild(
      ui::scrollView({
          .out = &m_scroll,
          .contentScale = contentScale(),
          .scrollbarVisible = true,
          .viewportPaddingH = 0.0F,
          .viewportPaddingV = 0.0F,
          .onScrollChanged = [this](float) { captureNextSnapshot(); },
          .configure =
              [](ScrollView& view) {
                view.clearFill();
                view.clearBorder();
                view.setReserveScrollbarGutter(true);
              },
      })
  );
  m_scroll->content()->setDirection(FlexDirection::Vertical);
  m_scroll->content()->setAlign(m_expanded ? FlexAlign::Start : FlexAlign::Stretch);
  m_scroll->content()->setGap(0);
  m_scroll->content()->addChild(
      ui::scrollView({
          .out = &m_horizontal,
          .contentScale = contentScale(),
          .orientation = ScrollOrientation::Horizontal,
          .scrollbarVisible = false,
          .viewportPaddingH = 0.0F,
          .viewportPaddingV = 0.0F,
          .onScrollChanged = [this](float offset) { syncHorizontalScroll(offset); },
          .configure =
              [](ScrollView& view) {
                view.clearFill();
                view.clearBorder();
              },
      })
  );
  bindPan(m_scroll->viewportArea());
  bindPan(m_horizontal->viewportArea());
  m_horizontal->content()->setDirection(FlexDirection::Vertical);
  m_horizontal->content()->setAlign(FlexAlign::Stretch);
  m_horizontal->content()->setGap(scaled(Style::spaceXs));
  auto horizontalBar = std::make_unique<Scrollbar>();
  m_horizontalBar = horizontalBar.get();
  horizontalBar->setOrientation(ScrollOrientation::Horizontal);
  horizontalBar->setContentScale(contentScale());
  horizontalBar->setOnScrollChanged([this](float offset) { m_horizontal->setScrollOffset(offset); });
  root->addChild(std::move(horizontalBar));
  auto grip = ui::inputArea({
      .out = &m_resizeGrip,
      .acceptedButtons = InputArea::buttonMask(BTN_LEFT),
      .tooltip = i18n::tr("workspace-map.resize"),
      .width = scaled(20.0F),
      .height = scaled(20.0F),
      .zIndex = 10,
  });
  grip->addChild(
      ui::glyph(
          {.glyph = "arrows-diagonal",
           .glyphSize = scaled(12.0F),
           .color = colorSpecFromRole(ColorRole::OnSurfaceVariant)}
      )
  );
  grip->setOnPress([this](const InputArea::PointerData& data) {
    if (data.pressed && data.button == BTN_LEFT) {
      if (m_resizeWindow)
        m_resizeWindow(data.serial);
      else
        PanelManager::instance().beginPanelResize(data.sceneX, data.sceneY);
    }
  });
  root->addChild(std::move(grip));
  setRoot(std::move(root));
  m_rebuildPending = true;
}

bool WorkspaceMapPanel::includesWindow(const InspectorWindowDetails& details, std::string_view output) {
  // `visible` is false on inactive workspaces. It must not be used as an
  // enumeration filter: each workspace owns its own coordinate plane below.
  return details.output == output
      && !details.workspaceId.empty()
      && details.x
      && details.y
      && details.width
      && details.height
      && *details.width > 0
      && *details.height > 0;
}

WorkspaceMapPanel::WorkspaceMap& WorkspaceMapPanel::ensureWorkspace(std::string_view id, std::string_view name) {
  const auto found = std::ranges::find(m_workspaces, id, &WorkspaceMap::id);
  if (found != m_workspaces.end())
    return *found;
  m_workspaces.push_back({.id = std::string(id), .name = std::string(name.empty() ? id : name)});
  return m_workspaces.back();
}

void WorkspaceMapPanel::rebuildMap() {
  m_heading->setText(i18n::tr("workspace-map.title") + " · " + m_output);
  auto* content = m_horizontal->content();
  while (!content->children().empty())
    content->removeChild(content->children().back().get());
  m_scroll->setScrollOffset(0);
  m_centerPending = true;
  if (m_workspaces.empty()) {
    content->addChild(
        ui::label({
            .text = i18n::tr("workspace-map.empty"),
            .fontSize = scaled(Style::fontSizeBody),
            .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
            .maxLines = 2,
        })
    );
  }
  for (auto& workspace : m_workspaces) {
    auto scene = ui::inputArea({.clipChildren = true});
    workspace.scene = scene.get();
    bindPan(scene.get());
    scene->addChild(
        ui::box({
            .out = &workspace.outputFill,
            .fill = colorSpecFromRole(ColorRole::SurfaceContainerLow),
            .radius = Style::scaledRadiusSm(contentScale()),
            .configure = [](Box& box) { box.setHitTestVisible(false); },
        })
    );
    for (auto& tile : workspace.tiles) {
      auto area = ui::inputArea({
          .out = &tile.area,
          .acceptedButtons = InputArea::buttonMask(BTN_LEFT),
          .tooltip = tile.title,
          .zIndex = tile.focused ? 1 : 0,
          .clipChildren = true,
      });
      bindPan(area.get());
      area->setOnClick([this, id = tile.id](const InputArea::PointerData&) {
        if (m_pan.moved || !m_platform.inspectorWindowDetails(id))
          return;
        dismiss();
        m_platform.focusCompositorWindow(id);
      });
      area->addChild(
          ui::box({
              .out = &tile.box,
              .fill = colorSpecFromRole(tile.focused ? ColorRole::PrimaryContainer : ColorRole::SurfaceContainer),
              .border = colorSpecFromRole(tile.focused ? ColorRole::Primary : ColorRole::OutlineVariant),
              .borderWidth = scaled(tile.focused ? 2.0F : 1.0F),
              .radius = scaled(3.0F),
          })
      );
      tile.previewUploadAttempted = false;
      area->addChild(ui::image({.out = &tile.preview, .fit = ImageFit::Cover, .configure = [](Image& image) {
                                  image.setHitTestVisible(false);
                                }}));
      tile.iconAttempted = false;
      area->addChild(ui::image({.out = &tile.icon, .fit = ImageFit::Contain}));
      area->addChild(
          ui::glyph({
              .out = &tile.fallbackIcon,
              .glyph = "window",
              .glyphSize = scaled(16.0F),
              .color = colorSpecFromRole(tile.focused ? ColorRole::OnPrimaryContainer : ColorRole::OnSurfaceVariant),
          })
      );
      area->addChild(
          ui::label({
              .out = &tile.label,
              .text = tile.appName,
              .fontSize = scaled(Style::fontSizeCaption),
              .color = colorSpecFromRole(tile.focused ? ColorRole::OnPrimaryContainer : ColorRole::OnSurface),
              .maxLines = 1,
          })
      );
      auto close = ui::inputArea({
          .out = &tile.close,
          .acceptedButtons = InputArea::buttonMask(BTN_LEFT),
          .tooltip = i18n::tr("window-inspector.close"),
          .zIndex = 4,
      });
      bindPan(close.get());
      close->setCursorShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
      close->setOnClick([this, id = tile.id](const InputArea::PointerData&) { closeWindow(id); });
      close->addChild(
          ui::glyph({
              .out = &tile.closeGlyph,
              .glyph = "close",
              .glyphSize = scaled(14.0F),
              .color = colorSpecFromRole(tile.focused ? ColorRole::OnPrimaryContainer : ColorRole::OnSurface),
          })
      );
      area->addChild(std::move(close));
      scene->addChild(std::move(area));
    }
    scene->addChild(
        ui::box({
            .out = &workspace.outputOutline,
            .fill = clearColorSpec(),
            .border = colorSpecFromRole(ColorRole::Secondary),
            .borderWidth = scaled(1.5F),
            .radius = Style::scaledRadiusSm(contentScale()),
            .configure = [](Box& box) {
              box.setZIndex(2);
              box.setHitTestVisible(false);
            },
        })
    );
    content->addChild(std::move(scene));
  }
}

void WorkspaceMapPanel::doLayout(Renderer& renderer, float width, float height) {
  m_snapshotLayout = true;
  if (m_rebuildPending) {
    const float horizontal = m_horizontal->scrollOffset();
    const float vertical = m_scroll->scrollOffset();
    rebuildMap();
    if (m_preservePan) {
      m_centerPending = false;
      m_horizontal->requestScrollToOffset(horizontal);
      m_scroll->requestScrollToOffset(vertical);
      m_preservePan = false;
    }
    m_rebuildPending = false;
  }
  width = std::max(1.0F, width);
  root()->setFrameSize(width, height);
  if (m_resizeWindow) {
    m_resizeGrip->setCursorShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE);
    m_resizeGrip->setPosition(width - scaled(4.0F), height - scaled(4.0F));
  } else if (auto* manager = PanelManager::current()) {
    m_resizeGrip->setCursorShape(
        manager->resizeFromLeft() == manager->resizeFromTop() ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE
                                                              : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE
    );
    m_resizeGrip->setPosition(
        manager->resizeFromLeft() ? -scaled(16.0F) : width - scaled(4.0F),
        manager->resizeFromTop() ? -scaled(16.0F) : height - scaled(4.0F)
    );
  }
  m_resizeGrip->setVisible(true);
  m_resizeGrip->layout(renderer);
  m_previewToggle->setGlyph(m_livePreviews ? "media-pause" : "media-play");
  m_previewToggle->setTooltip(
      i18n::tr(m_livePreviews ? "window-inspector.pause-short" : "window-inspector.resume-short")
  );
  m_previewToggle->setEnabled(m_capture.available());
  m_header->setFrameSize(width, scaled(Style::controlHeightSm));
  m_header->layout(renderer);
  const float top = scaled(Style::controlHeightSm + Style::spaceMd);
  const float barHeight = m_horizontalBar->reservedThickness() + scaled(Style::spaceXs);
  const float mapHeight = std::max(1.0F, height - top - barHeight);
  m_horizontalBar->setPosition(0, top + mapHeight + scaled(Style::spaceXs));
  m_horizontalBar->setFrameSize(
      std::max(1.0F, width - m_scroll->scrollbarGutter()), m_horizontalBar->reservedThickness()
  );
  m_scroll->setPosition(0, top);
  m_scroll->setFrameSize(width, mapHeight);
  // All workspaces belong to this one monitor. Fit the union once, then use
  // that identical transform for every row, including empty workspaces.
  std::vector<Tile> projectedTiles;
  for (const auto& workspace : m_workspaces) {
    for (const auto& tile : workspace.tiles)
      projectedTiles.push_back({.x = tile.x, .y = tile.y, .width = tile.width, .height = tile.height});
  }
  if (projectedTiles.empty()) {
    projectedTiles.push_back(
        {.x = m_outputBounds.x,
         .y = m_outputBounds.y,
         .width = std::max(1.0F, m_outputBounds.width),
         .height = std::max(1.0F, m_outputBounds.height)}
    );
  }
  const float sectionWidth = std::max(1.0F, m_scroll->contentViewportWidth(true));
  // Readable monitor-relative scale is independent of window/workspace count
  // and popup width. Extra columns extend the shared scrollable canvas.
  const float targetHeight = monitorMapHeight(m_outputBounds, contentScale());
  const float mapPadding = scaled(3.0F);
  const bool portrait = m_outputBounds.height > m_outputBounds.width;
  const auto viewport = normalizeTiles(
      projectedTiles, m_outputBounds, portrait || m_expanded ? scaled(32.0F) : sectionWidth, targetHeight, mapPadding,
      true
  );
  const float workspaceWidth = (viewport.x + viewport.width * 0.5F) * 2;
  const float compactHeight = (viewport.y + viewport.height * 0.5F) * 2;
  const float count = static_cast<float>(m_workspaces.size());
  const float gutter = scaled(Style::spaceXs);
  const float rawWidth = count * workspaceWidth + std::max(0.0F, count - 1) * gutter;
  const float sidePadding =
      portrait ? std::max(0.0F, (sectionWidth - (rawWidth > sectionWidth ? workspaceWidth : rawWidth)) * 0.5F) : 0;
  const float workspaceCanvasWidth =
      portrait ? std::max(sectionWidth, rawWidth + sidePadding * 2) : std::max(sectionWidth, workspaceWidth);
  // Keep grab space on every side, even when the complete map fits. These
  // margins belong to the canvas, never to the compositor geometry transform.
  const float panPaddingX = sectionWidth * 0.5F;
  const float panPaddingY = mapHeight * 0.5F;
  m_canvasWidth = workspaceCanvasWidth + panPaddingX * 2;
  if (m_centerPending)
    m_scroll->requestScrollToOffset(panPaddingY);
  else if (std::abs(panPaddingY - m_verticalPanPadding) > 0.5F)
    m_scroll->requestScrollToOffset(m_scroll->scrollOffset() + panPaddingY - m_verticalPanPadding);
  m_verticalPanPadding = panPaddingY;
  m_horizontal->content()->setDirection(portrait ? FlexDirection::Horizontal : FlexDirection::Vertical);
  m_horizontal->content()->setAlign(portrait ? FlexAlign::Start : FlexAlign::Stretch);
  m_horizontal->content()->setPadding(panPaddingY, sidePadding + panPaddingX, panPaddingY, sidePadding + panPaddingX);
  float contentHeight = 0;
  for (auto& workspace : m_workspaces) {
    const float rowHeight = workspace.tiles.empty() && !workspace.visible ? scaled(28.0F) : compactHeight;
    workspace.scene->setFrameSize(portrait ? workspaceWidth : workspaceCanvasWidth, rowHeight);
    contentHeight = portrait ? std::max(contentHeight, rowHeight) : contentHeight + rowHeight;
  }
  if (m_workspaces.size() > 1 && !portrait)
    contentHeight += static_cast<float>(m_workspaces.size() - 1) * gutter;
  contentHeight = std::max(contentHeight, scaled(Style::controlHeightSm));
  contentHeight += panPaddingY * 2;
  m_horizontal->content()->setFrameSize(m_canvasWidth, contentHeight);
  const float previousViewportWidth = m_horizontal->contentViewportWidth();
  const float relativePan = m_horizontal->scrollOffset() - m_horizontal->maxScrollOffset() * 0.5F;
  m_horizontal->setFrameSize(sectionWidth, contentHeight);
  if (m_centerPending || std::abs(previousViewportWidth - sectionWidth) > 0.5F) {
    float centeredOffset = (m_canvasWidth - sectionWidth) * 0.5F;
    if (portrait && rawWidth > sectionWidth) {
      const auto active =
          std::ranges::find_if(m_workspaces, [](const WorkspaceMap& workspace) { return workspace.visible; });
      const auto index = active == m_workspaces.end() ? 0 : std::distance(m_workspaces.begin(), active);
      centeredOffset = panPaddingX
          + sidePadding
          + static_cast<float>(index) * (workspaceWidth + gutter)
          + viewport.x
          + viewport.width * 0.5F
          - sectionWidth * 0.5F;
    }
    const float requestedOffset = m_centerPending         ? centeredOffset
        : portrait && m_horizontal->maxScrollOffset() > 0 ? m_horizontal->scrollOffset()
        : portrait                                        ? centeredOffset
                                                          : centeredOffset + relativePan;
    m_horizontal->requestScrollToOffset(std::max(0.0F, requestedOffset));
    m_centerPending = false;
  }
  m_scroll->layout(renderer);
  syncHorizontalScroll(m_horizontal->scrollOffset());
  std::size_t tileIndex = 0;
  for (auto& workspace : m_workspaces) {
    for (Box* box : {workspace.outputFill, workspace.outputOutline}) {
      box->setVisible(workspace.visible);
      box->setPosition(viewport.x, viewport.y);
      box->setFrameSize(viewport.width, viewport.height);
    }
    for (auto& tile : workspace.tiles) {
      tile.frame = projectedTiles[tileIndex++].frame;
      const auto& frame = tile.frame;
      tile.area->setPosition(frame.x, frame.y);
      tile.area->setFrameSize(frame.width, frame.height);
      tile.box->setFrameSize(frame.width, frame.height);
      const float closeSize = std::min({scaled(24.0F), frame.width, frame.height});
      tile.close->setPosition(std::max(0.0F, frame.width - closeSize - scaled(2.0F)), scaled(2.0F));
      tile.close->setFrameSize(closeSize, closeSize);
      tile.close->setVisible(closeSize >= scaled(16.0F));
      tile.closeGlyph->measure(renderer);
      tile.closeGlyph->setPosition(
          (closeSize - tile.closeGlyph->width()) * 0.5F, (closeSize - tile.closeGlyph->height()) * 0.5F
      );
      const float padding = scaled(8.0F);
      const float iconSize = scaled(16.0F);
      const float gap = scaled(6.0F);
      const float titleHeight = padding * 2 + iconSize;
      const float previewInset = scaled(tile.focused ? 2.0F : 1.0F);
      tile.preview->setPosition(previewInset, previewInset);
      tile.preview->setFrameSize(
          std::max(0.0F, frame.width - previewInset * 2), std::max(0.0F, frame.height - previewInset * 2)
      );
      if (const auto snapshot = m_snapshots.find(tile.id);
          snapshot != m_snapshots.end() && snapshot->second.image && !tile.previewUploadAttempted) {
        tile.previewUploadAttempted = true;
        const auto& image = *snapshot->second.image;
        tile.preview->setSourceRaw(
            renderer, image.rgba.data(), image.rgba.size(), image.width, image.height, image.width * 4,
            PixmapFormat::RGBA, false
        );
      }
      tile.preview->setVisible(tile.preview->hasImage());
      const bool fitsIcon = frame.width >= padding * 2 + iconSize + closeSize && frame.height >= titleHeight;
      const bool fitsName = fitsIcon && frame.width >= padding * 2 + iconSize + gap + scaled(32.0F) + closeSize;
      if (!tile.iconAttempted && !tile.iconPath.empty()) {
        tile.iconAttempted = true;
        tile.icon->setSourceFile(
            renderer, tile.iconPath, static_cast<int>(std::ceil(iconSize * renderer.renderScale()))
        );
      }
      const float iconX = fitsName ? padding : (frame.width - closeSize - iconSize) * 0.5F;
      tile.icon->setPosition(iconX, padding);
      tile.icon->setFrameSize(iconSize, iconSize);
      tile.icon->setVisible(fitsIcon && tile.icon->hasImage());
      tile.fallbackIcon->setVisible(fitsIcon && !tile.icon->hasImage());
      if (tile.fallbackIcon->visible()) {
        tile.fallbackIcon->measure(renderer);
        tile.fallbackIcon->setPosition(iconX, padding);
      }
      const bool offscreen = frame.x + frame.width <= viewport.x
          || frame.x >= viewport.x + viewport.width
          || frame.y + frame.height <= viewport.y
          || frame.y >= viewport.y + viewport.height;
      tile.area->setOpacity(offscreen && !tile.focused ? 0.55F : 1.0F);
      const float textX = padding + iconSize + gap;
      tile.label->setMaxWidth(std::max(1.0F, frame.width - textX - padding - closeSize));
      tile.label->measure(renderer);
      tile.label->setPosition(textX, (titleHeight - tile.label->height()) * 0.5F);
      tile.label->setVisible(fitsName);
    }
  }
  m_snapshotLayout = false;
  captureNextSnapshot();
}

void WorkspaceMapPanel::syncHorizontalScroll(float offset) {
  m_horizontalBar->update(m_horizontal->contentViewportWidth(), m_canvasWidth, offset);
  captureNextSnapshot();
}

LayoutRect WorkspaceMapPanel::normalizeTiles(
    std::vector<Tile>& tiles, const LayoutRect& outputBounds, float width, float height, float padding,
    bool scrollCanvas
) {
  if (tiles.empty())
    return {};
  double left = tiles.front().x, upper = tiles.front().y;
  double right = left + tiles.front().width, bottom = upper + tiles.front().height;
  for (const auto& tile : tiles) {
    left = std::min(left, tile.x);
    upper = std::min(upper, tile.y);
    right = std::max(right, tile.x + tile.width);
    bottom = std::max(bottom, tile.y + tile.height);
  }
  // IPC window positions are global layout targets. Include the output in the
  // same world bounds: off-screen scrolling columns must not be clipped to the
  // monitor, reflowed into a list, or independently enlarged for their labels.
  if (outputBounds.width > 0 && outputBounds.height > 0) {
    left = std::min(left, static_cast<double>(outputBounds.x));
    upper = std::min(upper, static_cast<double>(outputBounds.y));
    right = std::max(right, static_cast<double>(outputBounds.x) + outputBounds.width);
    bottom = std::max(bottom, static_cast<double>(outputBounds.y) + outputBounds.height);
    const double centerX = static_cast<double>(outputBounds.x) + outputBounds.width * 0.5;
    const double centerY = static_cast<double>(outputBounds.y) + outputBounds.height * 0.5;
    const double radiusX = std::max(centerX - left, right - centerX);
    const double radiusY = std::max(centerY - upper, bottom - centerY);
    left = centerX - radiusX;
    right = centerX + radiusX;
    upper = centerY - radiusY;
    bottom = centerY + radiusY;
  }
  padding = std::clamp(padding, 0.0F, std::max(0.0F, (std::min(width, height) - 1.0F) * 0.5F));
  const double scale = scrollCanvas
      ? (height - padding * 2) / (outputBounds.height > 0 ? outputBounds.height : bottom - upper)
      : std::min((width - padding * 2) / (right - left), (height - padding * 2) / (bottom - upper));
  const float canvasWidth =
      scrollCanvas ? std::max(width, static_cast<float>((right - left) * scale) + padding * 2) : width;
  const float canvasHeight = scrollCanvas ? static_cast<float>((bottom - upper) * scale) + padding * 2 : height;
  const float offsetX = (canvasWidth - static_cast<float>((right - left) * scale)) * 0.5F;
  const float offsetY = (canvasHeight - static_cast<float>((bottom - upper) * scale)) * 0.5F;
  const auto project = [&](double x, double y, double w, double h) -> LayoutRect {
    return {
        offsetX + static_cast<float>((x - left) * scale), offsetY + static_cast<float>((y - upper) * scale),
        static_cast<float>(w * scale), static_cast<float>(h * scale)
    };
  };
  for (auto& tile : tiles) {
    tile.frame = project(tile.x, tile.y, tile.width, tile.height);
  }
  return project(outputBounds.x, outputBounds.y, outputBounds.width, outputBounds.height);
}
